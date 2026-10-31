package omni.agent;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.security.ProtectionDomain;
import java.util.Arrays;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

// The live map's feed: the player and the living entities around them, 10 times a
// second, the blocks around the player as they change (Blocks) and the look of each kind
// of mob (Models). It only reads, through reflection, so it needs no hook in the game's code:
// the copy is made on the game thread by handing it to Minecraft as a task (Minecraft is
// an Executor), and turned into bytes and sent here, on the agent's thread.
final class Telemetry {
    private static final String MINECRAFT = "net.minecraft.client.Minecraft";
    private static final String CLIENT_LEVEL = "net.minecraft.client.multiplayer.ClientLevel";
    private static final String LEVEL = "net.minecraft.world.level.Level";
    private static final String SERVER = "net.minecraft.server.MinecraftServer";
    private static final String STORAGE = "net.minecraft.world.level.storage.LevelStorageSource$LevelStorageAccess";
    private static final String ENTITY = "net.minecraft.world.entity.Entity";
    private static final String ENTITY_TYPE = "net.minecraft.world.entity.EntityType";
    private static final String LIVING = "net.minecraft.world.entity.LivingEntity";
    private static final String ENEMY = "net.minecraft.world.entity.monster.Enemy";
    private static final String PLAYER = "net.minecraft.world.entity.player.Player";
    private static final String SERVER_DATA = "net.minecraft.client.multiplayer.ServerData";

    private static final int INTERVAL_MS = 100;
    private static final double RADIUS = 160;
    private static final int MAX_ENTITIES = 2000;

    private final Instrumentation instrumentation;
    private final String mappings;
    private final Link link;
    private final AtomicReference<Snapshot> latest = new AtomicReference<>();
    private final AtomicBoolean pending = new AtomicBoolean();
    private final Blocks blocks = new Blocks();
    private volatile boolean blocksOn;
    private final Models models = new Models();
    private volatile boolean modelsOn;
    private volatile ClassLoader gameLoader;

    private Method getInstance, entitiesForRendering, dimension, getSingleplayerServer, getLevelId, getCurrentServer;
    private Method getX, getY, getZ, getYRot, getType, getDescriptionId;
    private Field playerField, levelField, storageSource, bodyYaw, serverAddress;
    private Class<?> living, enemy, playerClass;
    private final Map<Object, String> typeIds = new IdentityHashMap<>();
    private Object lastLevel;
    private String lastWorld = "", lastDimension = "";
    private byte[] lastSent;
    private volatile int failures;

    Telemetry(Instrumentation instrumentation, String mappings, Link link) {
        this.instrumentation = instrumentation;
        this.mappings = mappings;
        this.link = link;
    }

    void run() throws Exception {
        final Names names = names(mappings);
        final String minecraft = names.className(MINECRAFT);
        if (minecraft == null) {
            link.status("unsupported: " + MINECRAFT + " is not in the mappings");
            return;
        }
        link.status("waiting for the game");
        final ClassLoader loader = waitForClass(minecraft);
        if (loader == null) {
            // A loader with names of its own (Fabric before 26.1: intermediary).
            link.status("unsupported: " + minecraft + " never loaded");
            return;
        }
        names.setLoader(loader);
        try {
            resolve(names);
        } catch (ReflectiveOperationException | LinkageError error) {
            // A name this version doesn't have (or a loader with its own names).
            link.status("unsupported: " + error);
            return;
        }
        try {
            blocks.resolve(names);
            blocksOn = true;
            link.status("ready");
        } catch (ReflectiveOperationException | LinkageError error) {
            link.status("ready, without live blocks: " + error);
        }
        try {
            models.resolve(names);
            modelsOn = true;
        } catch (ReflectiveOperationException | LinkageError error) {
            // The map draws boxes.
        }

        final Runnable copy = () -> {
            try {
                latest.set(snapshot());
                failures = 0;
            } catch (Throwable error) {
                if (++failures == 50)
                    link.status("snapshot failing: " + error);
            } finally {
                pending.set(false);
            }
        };
        while (failures < 50) {
            Thread.sleep(INTERVAL_MS);
            final Snapshot snapshot = latest.getAndSet(null);
            if (snapshot != null) {
                send(snapshot);
                for (Link.Frame chunk : snapshot.chunks)
                    link.send(chunk);
                if (snapshot.model != null)
                    link.send(snapshot.model);
            }
            if (!link.ready())
                continue;
            final Object game = getInstance.invoke(null);
            // One copy at a time: while the game is loading its queue may not run for a while.
            if (game instanceof Executor && pending.compareAndSet(false, true))
                ((Executor) game).execute(copy);
        }
        link.status("stopped");
    }

    static Names names(String mappings) throws java.io.IOException {
        final Set<String> classes = new HashSet<>(Arrays.asList(
                MINECRAFT, CLIENT_LEVEL, LEVEL, SERVER, STORAGE, ENTITY, ENTITY_TYPE, LIVING, ENEMY, PLAYER, SERVER_DATA));
        classes.addAll(Arrays.asList(Blocks.classes()));
        classes.addAll(Arrays.asList(Models.classes()));
        return new Names(mappings, classes);
    }

    // Every name the feed reads; throws on the first one this game doesn't have.
    void resolve(Names names) throws ReflectiveOperationException {
        getInstance = names.method(MINECRAFT, "getInstance");
        playerField = names.field(MINECRAFT, "player");
        levelField = names.field(MINECRAFT, "level");
        getSingleplayerServer = names.method(MINECRAFT, "getSingleplayerServer");
        entitiesForRendering = names.method(CLIENT_LEVEL, "entitiesForRendering");
        dimension = names.method(LEVEL, "dimension");
        storageSource = names.field(SERVER, "storageSource");
        getLevelId = names.method(STORAGE, "getLevelId");
        getCurrentServer = names.method(MINECRAFT, "getCurrentServer");
        serverAddress = names.field(SERVER_DATA, "ip");
        getX = names.method(ENTITY, "getX");
        getY = names.method(ENTITY, "getY");
        getZ = names.method(ENTITY, "getZ");
        getYRot = names.method(ENTITY, "getYRot");
        bodyYaw = names.field(LIVING, "yBodyRot");
        getType = names.method(ENTITY, "getType");
        getDescriptionId = names.method(ENTITY_TYPE, "getDescriptionId");
        living = names.type(LIVING);
        enemy = names.type(ENEMY);
        playerClass = names.type(PLAYER);
    }

    // The loader holding the game's classes, once the game class is loaded; null if it
    // isn't within 3 minutes.
    private ClassLoader waitForClass(String name) throws InterruptedException {
        final String internal = name.replace('.', '/');
        final ClassFileTransformer watch = new ClassFileTransformer() {
            @Override
            public byte[] transform(ClassLoader loader, String className, Class<?> redefined,
                                    ProtectionDomain domain, byte[] bytes) {
                if (gameLoader == null && internal.equals(className))
                    gameLoader = loader == null ? ClassLoader.getSystemClassLoader() : loader;
                return null;
            }
        };
        instrumentation.addTransformer(watch);
        try {
            for (Class<?> loaded : instrumentation.getAllLoadedClasses())
                if (loaded.getName().equals(name))
                    gameLoader = loaded.getClassLoader();
            final long giveUp = System.currentTimeMillis() + 180_000;
            while (gameLoader == null) {
                if (System.currentTimeMillis() > giveUp)
                    return null;
                Thread.sleep(250);
            }
        } finally {
            instrumentation.removeTransformer(watch);
        }
        // The class is being defined: let it finish, and the game reach its main loop.
        Thread.sleep(1000);
        return gameLoader;
    }

    // On the game thread: copy what the map needs, nothing more.
    private Snapshot snapshot() throws Exception {
        final Object game = getInstance.invoke(null);
        final Snapshot s = new Snapshot();
        final Object level = game == null ? null : levelField.get(game);
        final Object player = game == null ? null : playerField.get(game);
        if (level == null) {
            lastLevel = null;
            return s;
        }
        if (level != lastLevel) {
            lastLevel = level;
            lastDimension = resourceId(String.valueOf(dimension.invoke(level)));
            // The world's folder in singleplayer; on a server, "@" and the address joined
            // (the launcher knows the worlds of the servers it hosts by their port).
            final Object server = getSingleplayerServer.invoke(game);
            final Object joined = server == null ? getCurrentServer.invoke(game) : null;
            lastWorld = server != null ? String.valueOf(getLevelId.invoke(storageSource.get(server)))
                      : joined != null ? "@" + serverAddress.get(joined)
                      : "";
        }
        s.world = lastWorld;
        s.dimension = lastDimension;
        double px = 0, pz = 0;
        if (player != null) {
            s.hasPlayer = true;
            s.x = px = (Double) getX.invoke(player);
            s.y = (Double) getY.invoke(player);
            s.z = pz = (Double) getZ.invoke(player);
            s.yaw = (Float) getYRot.invoke(player);
        }
        for (Object entity : (Iterable<?>) entitiesForRendering.invoke(level)) {
            if (s.count == MAX_ENTITIES)
                break;
            if (!living.isInstance(entity) || playerClass.isInstance(entity))
                continue;
            final double x = (Double) getX.invoke(entity), z = (Double) getZ.invoke(entity);
            if (s.hasPlayer && (x - px) * (x - px) + (z - pz) * (z - pz) > RADIUS * RADIUS)
                continue;
            final int i = s.add();
            s.ex[i] = (float) x;
            s.ey[i] = ((Double) getY.invoke(entity)).floatValue();
            s.ez[i] = (float) z;
            s.eyaw[i] = bodyYaw.getFloat(entity);
            s.hostile[i] = enemy.isInstance(entity);
            final Object type = getType.invoke(entity);
            String id = typeIds.get(type);
            if (id == null) {
                id = typeId(String.valueOf(getDescriptionId.invoke(type)));
                typeIds.put(type, id);
            }
            s.type[i] = id;
            if (modelsOn && s.model == null) {
                try {
                    s.model = models.next(game, entity, id, link.connections());
                } catch (Throwable error) {
                    // That kind stays a box.
                }
            }
        }
        if (blocksOn && s.hasPlayer) {
            try {
                s.chunks = blocks.scan(level, lastDimension, px, pz, link.connections());
            } catch (Throwable error) {
                // The entities go on without them.
                blocksOn = false;
                link.status("live blocks stopped: " + error);
            }
        }
        return s;
    }

    private void send(Snapshot s) throws java.io.IOException {
        final Link.Frame frame = new Link.Frame(Link.STATE);
        frame.data.writeUTF(s.world);
        frame.data.writeUTF(s.dimension);
        frame.data.writeBoolean(s.hasPlayer);
        frame.data.writeDouble(s.x);
        frame.data.writeDouble(s.y);
        frame.data.writeDouble(s.z);
        frame.data.writeFloat(s.yaw);
        frame.data.writeInt(s.count);
        for (int i = 0; i < s.count; ++i) {
            frame.data.writeFloat(s.ex[i]);
            frame.data.writeFloat(s.ey[i]);
            frame.data.writeFloat(s.ez[i]);
            frame.data.writeFloat(s.eyaw[i]);
            frame.data.writeBoolean(s.hostile[i]);
            frame.data.writeUTF(s.type[i]);
        }
        // Nothing moved (a paused game, an empty menu): nothing to say.
        final byte[] bytes = frame.bytes.toByteArray();
        if (lastSent != null && Arrays.equals(bytes, lastSent))
            return;
        lastSent = bytes;
        link.send(frame);
    }

    // "ResourceKey[minecraft:dimension / minecraft:overworld]" → "minecraft:overworld"
    static String resourceId(String key) {
        final int slash = key.lastIndexOf(" / ");
        final int end = key.lastIndexOf(']');
        return slash < 0 || end < slash ? key : key.substring(slash + 3, end);
    }

    // "entity.minecraft.zombie" → "minecraft:zombie"
    static String typeId(String description) {
        final String rest = description.startsWith("entity.") ? description.substring(7) : description;
        final int dot = rest.indexOf('.');
        return dot < 0 ? rest : rest.substring(0, dot) + ':' + rest.substring(dot + 1);
    }

    private static final class Snapshot {
        String world = "", dimension = "";
        boolean hasPlayer;
        double x, y, z;
        float yaw;
        int count;
        float[] ex = new float[64], ey = new float[64], ez = new float[64], eyaw = new float[64];
        boolean[] hostile = new boolean[64];
        String[] type = new String[64];
        java.util.List<Link.Frame> chunks = java.util.Collections.emptyList();
        Link.Frame model; // a kind of mob new to the launcher

        int add() {
            if (count == ex.length) {
                final int size = count * 2;
                ex = Arrays.copyOf(ex, size);
                ey = Arrays.copyOf(ey, size);
                ez = Arrays.copyOf(ez, size);
                eyaw = Arrays.copyOf(eyaw, size);
                hostile = Arrays.copyOf(hostile, size);
                type = Arrays.copyOf(type, size);
            }
            return count++;
        }
    }
}
