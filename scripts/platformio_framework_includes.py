Import("env")

import os


framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
for library in ("Network", "NetworkClientSecure", "WiFi"):
    env.AppendUnique(
        CPPPATH=[os.path.join(framework, "libraries", library, "src")]
    )
