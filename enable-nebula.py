import os
found = False
for name in ("default_cfg.ini", "default_config.ini.cmake", "default_config.ini"):
    p = os.path.join("data", name)
    if os.path.exists(p):
        s = open(p, encoding="utf-8").read()
        h = "[plugins_load_at_startup]"
        if "NebulaAI" not in s:
            if h in s:
                s = s.replace(h, h + "\nNebulaAI = true", 1)
            else:
                s += "\n" + h + "\nNebulaAI = true\n"
            open(p, "w", encoding="utf-8").write(s)
        print("NebulaAI enabled by default in", p)
        found = True
        break
if not found:
    print("WARNING: no default config found, Nebula AI not enabled by default")
