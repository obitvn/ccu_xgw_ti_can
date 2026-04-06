// CCS Debug Startup Script for Core3 (R5F3)
// Executes when debug session starts

// Import CCS scripting
importPackage(Packages.com.ti.ccstudio.debug.server)

// Get debug session
var session = ds.getScriptingServer().getSessionMonitor().getScript()

// Get Core3 (index 3 for Cortex_R5_3)
var core = session.getScript().getTarget(3)

// Reset and halt on debug start
if (core != null) {
    // Reset the core
    core.reset()

    // Halt the core
    core.halt()

    // Load program symbols (CCS will do this automatically)
    // core.getMemory().loadProgram("${workspaceFolder}/ccu_ti_multi_core_realtime/Debug/ccu_ti_multi_core_realtime.out")
}

// Cleanup on disconnect
function onDisconnect() {
    if (core != null) {
        // Resume execution before disconnect
        core.run()
    }
}

// Register cleanup handler
// session.getScript().setOnDisconnectCallback(onDisconnect)
