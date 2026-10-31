package omni.agent;

import java.lang.ref.WeakReference;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

// The blocks around the player as the game has them now, before any save: each chunk
// within reach is sent once when it comes into reach, and again whenever a block in it
// changes. A change is seen through the chunk's "unsaved" mark, which the game sets on
// every block it sets; the client never saves, so the mark is cleared here after a look.
// Runs on the game thread, a few chunks a call, the changed ones first. Each section
// also carries its sky and block light as the game has it; the game spreads light a few
// ticks after a block changes, and into the chunks around, so a changed chunk and its
// neighbours are sent once more a moment later.
final class Blocks {
    static final String LEVEL = "net.minecraft.world.level.Level";
    static final String CHUNK_ACCESS = "net.minecraft.world.level.chunk.ChunkAccess";
    static final String LEVEL_CHUNK = "net.minecraft.world.level.chunk.LevelChunk";
    static final String SECTION = "net.minecraft.world.level.chunk.LevelChunkSection";
    static final String HEIGHT = "net.minecraft.world.level.LevelHeightAccessor";
    static final String LIGHT_ENGINE = "net.minecraft.world.level.lighting.LevelLightEngine";
    static final String LIGHT_LISTENER = "net.minecraft.world.level.lighting.LayerLightEventListener";
    static final String LIGHT_LAYER = "net.minecraft.world.level.LightLayer";
    static final String SECTION_POS = "net.minecraft.core.SectionPos";
    static final String DATA_LAYER = "net.minecraft.world.level.chunk.DataLayer";

    private static final int REACH = 6;          // chunks around the player
    private static final int PER_CALL = 2;       // chunks read and sent per call
    private static final int PALETTE_BYTES = 256;
    private static final long SETTLE_MS = 1500; // the light spreading after a change

    // Light of a section, sky then block: unknown, one level all through, or 2048 bytes.
    private static final int LIGHT_UNKNOWN = 0, LIGHT_UNIFORM = 1, LIGHT_FULL = 2;

    private Method getChunk, getSections, getBlockState, hasOnlyAir, minSection;
    private Method getLightEngine, getLayerListener, getDataLayerData, sectionPos, lightData, lightEmpty, lightAt;
    private Object[] lightLayers; // SKY, BLOCK
    private Field unsaved;
    private Class<?> levelChunk;

    private Object level;
    private int connection = -1;
    private final Map<Long, WeakReference<Object>> seen = new HashMap<>();
    private final Set<Long> changed = new LinkedHashSet<>();
    private final Set<Long> unseen = new LinkedHashSet<>();
    private final Map<Long, Long> again = new HashMap<>(); // chunk → when to send it again
    private final Map<Object, String> stateNames = new IdentityHashMap<>();

    static String[] classes() {
        return new String[] {LEVEL, CHUNK_ACCESS, LEVEL_CHUNK, SECTION, HEIGHT, LIGHT_ENGINE, LIGHT_LISTENER, LIGHT_LAYER,
                             SECTION_POS, DATA_LAYER};
    }

    void resolve(Names names) throws ReflectiveOperationException {
        getChunk = names.method(LEVEL, "getChunk", int.class, int.class);
        getSections = names.method(CHUNK_ACCESS, "getSections");
        unsaved = names.field(CHUNK_ACCESS, "unsaved");
        getBlockState = names.method(SECTION, "getBlockState", int.class, int.class, int.class);
        hasOnlyAir = names.method(SECTION, "hasOnlyAir");
        levelChunk = names.type(LEVEL_CHUNK);
        try {
            minSection = names.method(HEIGHT, "getMinSectionY"); // 1.21.2+
        } catch (NoSuchMethodException older) {
            minSection = names.method(HEIGHT, "getMinSection");
        }
        getLightEngine = names.method(LEVEL, "getLightEngine");
        final Class<?> layer = names.type(LIGHT_LAYER);
        lightLayers = layer.getEnumConstants();
        getLayerListener = names.method(LIGHT_ENGINE, "getLayerListener", layer);
        final Class<?> position = names.type(SECTION_POS);
        getDataLayerData = names.method(LIGHT_LISTENER, "getDataLayerData", position);
        sectionPos = names.method(SECTION_POS, "of", int.class, int.class, int.class);
        lightData = names.method(DATA_LAYER, "getData");
        lightEmpty = names.method(DATA_LAYER, "isEmpty");
        lightAt = names.method(DATA_LAYER, "get", int.class, int.class, int.class);
    }

    // On the game thread: the chunks to send now, as Blocks frames.
    List<Link.Frame> scan(Object level, String dimension, double x, double z, int connection) throws Exception {
        if (level != this.level || connection != this.connection) {
            // Another world or dimension, or a launcher that knows nothing yet.
            this.level = level;
            this.connection = connection;
            seen.clear();
            changed.clear();
            unseen.clear();
            again.clear();
        }
        final long now = System.currentTimeMillis();
        for (Iterator<Map.Entry<Long, Long>> it = again.entrySet().iterator(); it.hasNext();) {
            final Map.Entry<Long, Long> entry = it.next();
            if (entry.getValue() <= now) {
                if (seen.containsKey(entry.getKey()))
                    unseen.add(entry.getKey());
                it.remove();
            }
        }
        final int px = (int) Math.floor(x) >> 4, pz = (int) Math.floor(z) >> 4;
        for (int cz = pz - REACH; cz <= pz + REACH; ++cz)
            for (int cx = px - REACH; cx <= px + REACH; ++cx) {
                final Object chunk = getChunk.invoke(level, cx, cz);
                // Not loaded: the game hands out an empty stand-in of another class.
                if (chunk == null || chunk.getClass() != levelChunk)
                    continue;
                final long key = key(cx, cz);
                final WeakReference<Object> known = seen.get(key);
                if (known == null || known.get() != chunk) {
                    seen.put(key, new WeakReference<>(chunk));
                    unsaved.setBoolean(chunk, false);
                    unseen.add(key);
                } else if (unsaved.getBoolean(chunk)) {
                    unsaved.setBoolean(chunk, false);
                    changed.add(key);
                }
            }
        // Out of reach: forget, so coming back sends it again.
        for (Iterator<Long> it = seen.keySet().iterator(); it.hasNext();) {
            final long key = it.next();
            if (Math.abs(chunkX(key) - px) > REACH + 1 || Math.abs(chunkZ(key) - pz) > REACH + 1) {
                it.remove();
                changed.remove(key);
                unseen.remove(key);
            }
        }

        final List<Link.Frame> frames = new ArrayList<>();
        final ArrayDeque<Long> next = new ArrayDeque<>(changed);
        next.addAll(unseen);
        while (frames.size() < PER_CALL && !next.isEmpty()) {
            final long key = next.poll();
            final boolean wasChanged = changed.remove(key);
            unseen.remove(key);
            final WeakReference<Object> known = seen.get(key);
            final Object chunk = known == null ? null : known.get();
            if (chunk == null)
                continue;
            frames.add(frame(level, chunk, dimension, chunkX(key), chunkZ(key)));
            if (wasChanged)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        again.put(key(chunkX(key) + dx, chunkZ(key) + dz), now + SETTLE_MS);
        }
        return frames;
    }

    // Blocks: dimension, chunk x and z, its lowest section, then each section bottom up:
    // u16 palette size, the palette as block states ("Block{minecraft:stone}[...]"), and
    // with more than one, 4096 indices (y, z, x), a byte each up to 256, else two; then
    // its sky and block light (light()).
    private Link.Frame frame(Object level, Object chunk, String dimension, int cx, int cz) throws Exception {
        final Object[] sections = (Object[]) getSections.invoke(chunk);
        final Link.Frame frame = new Link.Frame(Link.BLOCKS);
        final int lowest = (Integer) minSection.invoke(chunk);
        frame.data.writeUTF(dimension);
        frame.data.writeInt(cx);
        frame.data.writeInt(cz);
        frame.data.writeInt(lowest);
        frame.data.writeShort(sections.length);
        final Map<Object, Integer> palette = new IdentityHashMap<>();
        final List<Object> order = new ArrayList<>();
        final int[] indices = new int[4096];
        final Object engine = getLightEngine.invoke(level);
        for (int s = 0; s < sections.length; ++s) {
            final Object section = sections[s];
            palette.clear();
            order.clear();
            if (section == null || (Boolean) hasOnlyAir.invoke(section)) {
                frame.data.writeShort(0); // air
                light(frame, engine, cx, lowest + s, cz);
                continue;
            }
            for (int i = 0; i < 4096; ++i) {
                final Object state = getBlockState.invoke(section, i & 15, i >> 8, (i >> 4) & 15);
                Integer index = palette.get(state);
                if (index == null) {
                    index = order.size();
                    palette.put(state, index);
                    order.add(state);
                }
                indices[i] = index;
            }
            frame.data.writeShort(order.size());
            for (Object state : order)
                frame.data.writeUTF(name(state));
            if (order.size() > 1)
                for (int i = 0; i < 4096; ++i) {
                    if (order.size() <= PALETTE_BYTES)
                        frame.data.writeByte(indices[i]);
                    else
                        frame.data.writeShort(indices[i]);
                }
            light(frame, engine, cx, lowest + s, cz);
        }
        return frame;
    }

    // u8 kind per layer (sky, block): unknown; uniform, then u8 level; full, then 2048
    // bytes of 4-bit levels, as saved in a chunk (index y, z, x, low nibble first).
    private void light(Link.Frame frame, Object engine, int cx, int sy, int cz) throws Exception {
        final Object position = sectionPos.invoke(null, cx, sy, cz);
        for (Object layer : lightLayers) {
            final Object listener = getLayerListener.invoke(engine, layer);
            final Object data = listener == null ? null : getDataLayerData.invoke(listener, position);
            if (data == null) {
                frame.data.writeByte(LIGHT_UNKNOWN);
            } else if ((Boolean) lightEmpty.invoke(data)) {
                frame.data.writeByte(LIGHT_UNIFORM);
                frame.data.writeByte((Integer) lightAt.invoke(data, 0, 0, 0));
            } else {
                frame.data.writeByte(LIGHT_FULL);
                frame.data.write((byte[]) lightData.invoke(data));
            }
        }
    }

    private String name(Object state) {
        String name = stateNames.get(state);
        if (name == null) {
            name = String.valueOf(state);
            stateNames.put(state, name);
        }
        return name;
    }

    static long key(int cx, int cz) {
        return ((long) cx << 32) | (cz & 0xFFFFFFFFL);
    }

    static int chunkX(long key) {
        return (int) (key >> 32);
    }

    static int chunkZ(long key) {
        return (int) key;
    }
}
