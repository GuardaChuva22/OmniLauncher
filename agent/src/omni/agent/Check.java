package omni.agent;

// Checks, outside the game, that every name the agent reads exists in a client:
//   java -cp omni-agent.jar;<client jar>;<its libraries> omni.agent.Check [mappings file]
public final class Check {
    private Check() {}

    public static void main(String[] arguments) throws Exception {
        final Names names = Telemetry.names(arguments.length > 0 ? arguments[0] : "");
        names.setLoader(Check.class.getClassLoader());
        new Telemetry(null, "", null).resolve(names);
        new Blocks().resolve(names);
        new Models().resolve(names);
        if (!Telemetry.resourceId("ResourceKey[minecraft:dimension / minecraft:the_nether]").equals("minecraft:the_nether")
                || !Telemetry.typeId("entity.minecraft.zombie_villager").equals("minecraft:zombie_villager"))
            throw new AssertionError("id parsing");
        System.out.println("OK");
    }
}
