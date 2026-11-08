import os
p = os.path.join("data", "default_config.ini")
s = open(p, encoding="utf-8").read()
h = "[plugins_load_at_startup]"
if "NebulaAI" not in s:
    if h in s:
        s = s.replace(h, h + "\nNebulaAI = true", 1)
    else:
        s += "\n" + h + "\nNebulaAI = true\n"
    open(p, "w", encoding="utf-8").write(s)
print("NebulaAI enabled by default")
