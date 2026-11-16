"""Edit installation settings while preserving unrelated local overrides."""
from copy import deepcopy
from dataclasses import dataclass
import json
from pathlib import Path
import re


@dataclass
class ShadeSettings:
    open_ms: int
    close_ms: int
    favourite: bool
    open_limit: bool
    close_limit: bool

    def validate(self):
        if not 100 <= self.open_ms <= 600000 or not 100 <= self.close_ms <= 600000:
            raise ValueError("Travel times must be between 0.1 and 600 seconds.")


# Order is shared by the header's lists, the editor and the loader.
SETTING_KEYS = {
    "open_ms": "SHADE_TRAVEL_OPEN_MS",
    "close_ms": "SHADE_TRAVEL_CLOSE_MS",
    "favourite": "SHADE_CLOSE_USES_FAVOURITE",
    "open_limit": "SHADE_OPEN_IS_HARD_LIMIT",
    "close_limit": "SHADE_CLOSE_IS_HARD_LIMIT",
}


@dataclass
class Installation:
    names: list[str]
    address: int
    defaults: ShadeSettings
    shades: list[ShadeSettings]

    def resize(self, names):
        if not 1 <= len(names) <= 255:
            raise ValueError("Enter between 1 and 255 shade names, one per line.")
        self.shades = [self.shades[i] if i < len(self.shades) else deepcopy(self.defaults)
                       for i in range(len(names))]
        self.names = names[:]

    def validate(self):
        if not 1 <= len(self.names) <= 255:
            raise ValueError("Enter between 1 and 255 shade names, one per line.")
        if any(not name.strip() or any(ord(c) < 32 for c in name) for name in self.names):
            raise ValueError("Shade names must be nonempty and contain no control characters.")
        if len(self.names) != len(self.shades):
            raise ValueError("Each shade must have its own settings.")
        if not 1 <= self.address <= 0xFFFFFF - len(self.names) + 1:
            raise ValueError("Choose a nonzero 24-bit address range that fits all shades.")
        for name, shade in zip(self.names, self.shades):
            try:
                shade.validate()
            except ValueError as error:
                raise ValueError(f"{name}: {error}") from error

    @classmethod
    def load(cls, project: Path):
        defaults_path = project / "include/somfy_config.h"
        if not defaults_path.exists():
            raise ValueError("Select the firmware repository with --project to edit configuration.")
        local = project / "include/somfy_config.local.h"
        values = {}
        for text in [defaults_path.read_text(encoding="utf-8"),
                     local.read_text(encoding="utf-8") if local.exists() else ""]:
            text = re.sub(r"\\\r?\n", " ", text)
            values.update(re.findall(r"^[ \t]*#define[ \t]+(\w+)[ \t]+([^\n]+)", text, re.M))

        def value(key):
            return re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//.*',
                          lambda match: match[0] if match[0].startswith('"') else "",
                          values[key]).strip()

        def number(raw):
            return int(re.sub(r"[uUlL]+$", "", raw), 0)

        def boolean(raw):
            if raw not in {"true", "false", "1", "0"}:
                raise ValueError("Flags must be a literal true or false.")
            return raw in {"true", "1"}

        def initializer(key):
            raw = value(key)
            if not raw.startswith("{") or not raw.endswith("}"):
                raise ValueError(f"{key} must be a literal initializer enclosed in braces.")
            return raw[1:-1]

        try:
            names = json.loads("[" + initializer("SHADE_NAMES") + "]")
            if not all(isinstance(name, str) for name in names):
                raise ValueError("Names must be strings.")
            if len(names) != number(value("SHADE_COUNT")):
                raise ValueError("SHADE_NAMES and SHADE_COUNT disagree.")
            shared = ShadeSettings(**{field: (number if field.endswith("_ms") else boolean)(value(key))
                                      for field, key in SETTING_KEYS.items()})
            shades = [deepcopy(shared) for _ in names]
            for field, key in SETTING_KEYS.items():
                per_shade_key = key + "_PER_SHADE"
                if per_shade_key not in values:
                    continue
                entries = [entry.strip() for entry in initializer(per_shade_key).split(",")]
                if entries and entries[-1] == "":
                    entries.pop()  # C allows a trailing comma.
                if len(entries) != len(names):
                    raise ValueError(f"{per_shade_key} must have exactly {len(names)} entries.")
                parse = number if field.endswith("_ms") else boolean
                for shade, entry in zip(shades, entries):
                    setattr(shade, field, parse(entry))
            config = cls(names, number(value("SOMFY_ADDR_BASE")), shared, shades)
            config.validate()
            return config
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"Cannot read literal configuration: {error}. Your file was not changed.") from error

    def save(self, project: Path):
        self.validate()
        path = project / "include/somfy_config.local.h"
        text = path.read_text(encoding="utf-8") if path.exists() else "/* Installation overrides, generated by somfy-setup. */\n"
        values = {
            "SHADE_COUNT": str(len(self.names)),
            "SHADE_NAMES": "{ " + ", ".join(json.dumps(n, ensure_ascii=False) for n in self.names) + " }",
            "SOMFY_ADDR_BASE": f"0x{self.address:06X}u",
        }
        for field, key in SETTING_KEYS.items():
            def literal(value):
                return str(value).lower() if isinstance(value, bool) else str(value)
            # Keep the legacy shared defaults, so newly added shades inherit them.
            values[key] = literal(getattr(self.defaults, field))
            values[key + "_PER_SHADE"] = "{ " + ", ".join(literal(getattr(s, field)) for s in self.shades) + " }"
        for key, value in values.items():
            pattern = rf"^[ \t]*#define[ \t]+{key}[ \t]+(?:[^\n]*\\\n)*[^\n]*"
            if re.search(pattern, text, re.M):
                text = re.sub(pattern, lambda _: f"#define {key} {value}", text, flags=re.M)
            else:
                end = list(re.finditer(r"^[ \t]*#endif\b", text, re.M))
                offset = end[-1].start() if end else len(text)
                text = text[:offset].rstrip() + f"\n#define {key} {value}\n" + text[offset:]
        if path.is_symlink():
            raise ValueError("The local configuration is a symlink; edit it directly.")
        temporary = path.with_suffix(".h.tmp")
        temporary.write_text(text, encoding="utf-8")
        temporary.replace(path)
        return path
