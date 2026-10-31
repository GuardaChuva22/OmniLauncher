package omni.agent;

import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

// The shape and skin of each kind of mob, as the game draws it: the model of its renderer
// (every cube of every part, at rest, the parts' moves and turns applied) and the texture
// it is drawn with, read from the game's resources, so a resource pack's or a mod's mob
// comes out as the game shows it. Sent once a connection per kind, as the kind is first
// seen; a kind the game draws some other way (no living renderer) is left to the map.
final class Models {
    static final String MINECRAFT = "net.minecraft.client.Minecraft";
    static final String DISPATCHER = "net.minecraft.client.renderer.entity.EntityRenderDispatcher";
    static final String RENDERER = "net.minecraft.client.renderer.entity.EntityRenderer";
    static final String LIVING_RENDERER = "net.minecraft.client.renderer.entity.LivingEntityRenderer";
    static final String LIVING_STATE = "net.minecraft.client.renderer.entity.state.LivingEntityRenderState";
    static final String ENTITY = "net.minecraft.world.entity.Entity";
    static final String PART = "net.minecraft.client.model.geom.ModelPart";
    static final String CUBE = "net.minecraft.client.model.geom.ModelPart$Cube";
    static final String POLYGON = "net.minecraft.client.model.geom.ModelPart$Polygon";
    static final String VERTEX = "net.minecraft.client.model.geom.ModelPart$Vertex";
    static final String RESOURCES = "net.minecraft.server.packs.resources.ResourceProvider";
    static final String IDENTIFIER = "net.minecraft.resources.Identifier";               // 1.21.11+
    static final String LOCATION = "net.minecraft.resources.ResourceLocation";

    private static final int MAX_QUADS = 4096;
    private static final int MAX_TEXTURE = 1 << 20;

    private Method getDispatcher, getRenderer, getResourceManager, open, resetPose;
    private Method createState, stateTexture; // 1.21.2+: the texture comes from a render state
    private Method entityTexture;             // before
    private Field model, partX, partY, partZ, xRot, yRot, zRot, xScale, yScale, zScale, visible, skipDraw;
    private Field cubes, children, polygons, vertices, vertexX, vertexY, vertexZ, vertexPos, vertexU, vertexV;
    private Field posX, posY, posZ;
    private Class<?> livingRenderer, part;

    private int connection = -1;
    private final Set<String> done = new HashSet<>();

    static String[] classes() {
        return new String[] {MINECRAFT, DISPATCHER, RENDERER, LIVING_RENDERER, LIVING_STATE, ENTITY, PART, CUBE, POLYGON, VERTEX,
                             RESOURCES, IDENTIFIER, LOCATION};
    }

    void resolve(Names names) throws ReflectiveOperationException {
        getDispatcher = names.method(MINECRAFT, "getEntityRenderDispatcher");
        getResourceManager = names.method(MINECRAFT, "getResourceManager");
        final Class<?> entity = names.type(ENTITY);
        getRenderer = names.method(DISPATCHER, "getRenderer", entity);
        livingRenderer = names.type(LIVING_RENDERER);
        model = names.field(LIVING_RENDERER, "model");
        Class<?> location;
        try {
            location = names.type(IDENTIFIER);
        } catch (ClassNotFoundException older) {
            location = names.type(LOCATION);
        }
        open = names.method(RESOURCES, "open", location);
        try {
            createState = names.method(RENDERER, "createRenderState", entity, float.class);
            stateTexture = names.method(LIVING_RENDERER, "getTextureLocation", names.type(LIVING_STATE));
        } catch (ReflectiveOperationException older) {
            entityTexture = names.method(RENDERER, "getTextureLocation", entity);
        }

        part = names.type(PART);
        partX = names.field(PART, "x");
        partY = names.field(PART, "y");
        partZ = names.field(PART, "z");
        xRot = names.field(PART, "xRot");
        yRot = names.field(PART, "yRot");
        zRot = names.field(PART, "zRot");
        xScale = names.fieldOrNull(PART, "xScale");
        yScale = names.fieldOrNull(PART, "yScale");
        zScale = names.fieldOrNull(PART, "zScale");
        visible = names.field(PART, "visible");
        skipDraw = names.fieldOrNull(PART, "skipDraw");
        cubes = names.field(PART, "cubes");
        children = names.field(PART, "children");
        try {
            resetPose = names.method(PART, "resetPose");
        } catch (NoSuchMethodException older) {
            resetPose = null;
        }
        polygons = names.field(CUBE, "polygons");
        vertices = names.field(POLYGON, "vertices");
        vertexPos = names.fieldOrNull(VERTEX, "pos"); // before 1.21.2 a Vector3f
        if (vertexPos == null) {
            vertexX = names.field(VERTEX, "x");
            vertexY = names.field(VERTEX, "y");
            vertexZ = names.field(VERTEX, "z");
        }
        vertexU = names.field(VERTEX, "u");
        vertexV = names.field(VERTEX, "v");
    }

    // On the game thread: the model of `entity`'s kind if it is new to this connection,
    // else null. At most one a call, as reading one takes a while.
    Link.Frame next(Object game, Object entity, String id, int connection) throws Exception {
        if (connection != this.connection) {
            this.connection = connection;
            done.clear();
        }
        if (!done.add(id))
            return null;
        final Object renderer = getRenderer.invoke(getDispatcher.invoke(game), entity);
        if (!livingRenderer.isInstance(renderer))
            return null;
        final Object root = model.get(renderer);
        final byte[] texture = texture(game, renderer, entity);
        if (root == null || texture == null)
            return null;

        final List<float[]> quads = new ArrayList<>();
        for (Object top : roots(root))
            walk(top, new float[] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, quads);
        if (quads.isEmpty())
            return null;

        // Model: id, the PNG (u32 size, bytes), u32 quads, each 4 × (x, y, z, u, v):
        // positions in the model's pixels (y down, as the game has them), u and v over
        // the whole texture.
        final Link.Frame frame = new Link.Frame(Link.MODEL);
        frame.data.writeUTF(id);
        frame.data.writeInt(texture.length);
        frame.data.write(texture);
        frame.data.writeInt(quads.size());
        for (float[] quad : quads)
            for (float value : quad)
                frame.data.writeFloat(value);
        return frame;
    }

    private byte[] texture(Object game, Object renderer, Object entity) throws Exception {
        final Object location = createState != null
                ? stateTexture.invoke(renderer, createState.invoke(renderer, entity, 1f))
                : entityTexture.invoke(renderer, entity);
        if (location == null)
            return null;
        try (InputStream in = (InputStream) open.invoke(getResourceManager.invoke(game), location)) {
            final byte[] bytes = in.readNBytes(MAX_TEXTURE + 1);
            return bytes.length > MAX_TEXTURE ? null : bytes;
        }
    }

    // The parts a model holds that no other of its parts holds: older models keep their
    // parts in fields of their own, newer ones one root.
    private List<Object> roots(Object root) throws Exception {
        final List<Object> parts = new ArrayList<>();
        for (Class<?> type = root.getClass(); type != null && type != Object.class; type = type.getSuperclass())
            for (Field field : type.getDeclaredFields()) {
                if (java.lang.reflect.Modifier.isStatic(field.getModifiers()) || !part.isAssignableFrom(field.getType()))
                    continue;
                field.setAccessible(true);
                final Object value = field.get(root);
                if (value != null && !parts.contains(value))
                    parts.add(value);
            }
        final Map<Object, Boolean> held = new IdentityHashMap<>();
        for (Object each : parts)
            hold(each, held, false);
        final List<Object> roots = new ArrayList<>();
        for (Object each : parts)
            if (!held.containsKey(each))
                roots.add(each);
        return roots;
    }

    private void hold(Object at, Map<Object, Boolean> held, boolean child) throws Exception {
        if (child && held.put(at, Boolean.TRUE) != null)
            return;
        for (Object next : ((Map<?, ?>) children.get(at)).values())
            hold(next, held, true);
    }

    // A part at rest, its move, turn (z, then y, then x, as the game turns it) and scale
    // laid over `parent` (3 × 4, row by row), then its cubes and its children.
    private void walk(Object at, float[] parent, List<float[]> quads) throws Exception {
        if (!visible.getBoolean(at) || quads.size() >= MAX_QUADS)
            return;
        if (resetPose != null)
            resetPose.invoke(at);
        final float[] local = local(at);
        final float[] m = multiply(parent, local);
        if (skipDraw == null || !skipDraw.getBoolean(at))
            for (Object cube : (Collection<?>) cubes.get(at))
                for (Object polygon : (Object[]) polygons.get(cube)) {
                    final Object[] corners = (Object[]) vertices.get(polygon);
                    if (corners.length != 4 || quads.size() >= MAX_QUADS)
                        continue;
                    final float[] quad = new float[20];
                    for (int i = 0; i < 4; ++i) {
                        final float x, y, z;
                        if (vertexPos != null) {
                            // A JOML Vector3f, of the game's loader: read through its fields.
                            final Object pos = vertexPos.get(corners[i]);
                            if (posX == null) {
                                posX = pos.getClass().getField("x");
                                posY = pos.getClass().getField("y");
                                posZ = pos.getClass().getField("z");
                            }
                            x = posX.getFloat(pos);
                            y = posY.getFloat(pos);
                            z = posZ.getFloat(pos);
                        } else {
                            x = vertexX.getFloat(corners[i]);
                            y = vertexY.getFloat(corners[i]);
                            z = vertexZ.getFloat(corners[i]);
                        }
                        quad[i * 5] = m[0] * x + m[1] * y + m[2] * z + m[3];
                        quad[i * 5 + 1] = m[4] * x + m[5] * y + m[6] * z + m[7];
                        quad[i * 5 + 2] = m[8] * x + m[9] * y + m[10] * z + m[11];
                        quad[i * 5 + 3] = vertexU.getFloat(corners[i]);
                        quad[i * 5 + 4] = vertexV.getFloat(corners[i]);
                    }
                    quads.add(quad);
                }
        for (Object child : ((Map<?, ?>) children.get(at)).values())
            walk(child, m, quads);
    }

    private float[] local(Object at) throws Exception {
        final double ax = xRot.getFloat(at), ay = yRot.getFloat(at), az = zRot.getFloat(at);
        final float cx = (float) Math.cos(ax), sx = (float) Math.sin(ax);
        final float cy = (float) Math.cos(ay), sy = (float) Math.sin(ay);
        final float cz = (float) Math.cos(az), sz = (float) Math.sin(az);
        // Rz · Ry · Rx
        final float[] r = {
            cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
            sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
            -sy, cy * sx, cy * cx};
        final float kx = xScale == null ? 1 : xScale.getFloat(at);
        final float ky = yScale == null ? 1 : yScale.getFloat(at);
        final float kz = zScale == null ? 1 : zScale.getFloat(at);
        return new float[] {
            r[0] * kx, r[1] * ky, r[2] * kz, partX.getFloat(at),
            r[3] * kx, r[4] * ky, r[5] * kz, partY.getFloat(at),
            r[6] * kx, r[7] * ky, r[8] * kz, partZ.getFloat(at)};
    }

    private static float[] multiply(float[] a, float[] b) {
        final float[] m = new float[12];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column)
                m[row * 4 + column] = a[row * 4] * b[column] + a[row * 4 + 1] * b[4 + column] + a[row * 4 + 2] * b[8 + column];
            m[row * 4 + 3] += a[row * 4 + 3];
        }
        return m;
    }
}
