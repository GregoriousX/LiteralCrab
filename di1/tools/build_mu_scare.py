"""Build an experimental MU scare companion from the user's original DI1 assets.

No original files are written. The ASI must include the matching companion
actor/dependency patches. This creates a prototype, not gameplay acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

MOD = "literalcrab_mu_scare"
ITEM = "ITEM_LITERALCRAB_MEGAPHONE"
TOOLSET_SHA = "dcd170e02658db0a533328e05850054ced65b0c7bae7013897344119c3d2baca"
SCRIPT_SHA = "f0dbfafcb21adf4f33064d9609f4186bcbe9b638c3dc33c08ace08bc32eaf9e9"
REACTION_SCRIPT_SHA = "a9ae651486d128a595b23c340cbe72855c14dcb237c69376f52a5084f4fe5818"
BIPEDS = frozenset("""AV_Buzz AV_Woody AV_Jessie AV_MrIncredible AV_ElastiGirl
AV_Dash AV_Violet AV_Syndrome AV_Mike AV_Sully AV_Randall AV_LoneRanger
AV_Tonto AV_JackSparrow AV_Barbossa AV_DavyJones AV_JackSkellington AV_Perry
AV_Phineas AV_MickeyMouse AV_Ralph AV_Vanellope AV_Anna AV_Elsa AV_Rapunzel
AV_Zurg""".split())


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tool_hash(name):
    # Original ToolSetInfo::CalcHash; all original actor/tool hashes are checked.
    value = ord(name[0]) << 7 if name else 0
    for char in name:
        value = ((value * 1000003) ^ ord(char)) & 0xFFFFFFFF
    return value ^ len(name)


def split_blocks(raw):
    blocks, at = [], 0
    while at < len(raw):
        size = struct.unpack_from("<I", raw, at)[0]
        require(size >= 4 and at + size <= len(raw), "Invalid binary block")
        blocks.append(raw[at:at + size])
        at += size
    require(len(blocks) == 13, "Unsupported tool table layout")
    return blocks


def build_table(raw):
    require(sha(raw) == TOOLSET_SHA, "Unsupported original toolset.bin")
    old = split_blocks(raw)
    blocks = [bytearray(b) for b in old]
    strings = bytearray(old[0][4:])
    stream = list(struct.unpack("<" + "I" * ((len(old[1]) - 4) // 4), old[1][4:]))

    def string(index):
        require(index != 0xFFFFFFFF and index < len(strings), "Invalid string")
        return strings[index:strings.index(0, index)].decode("ascii")

    def add_string(value):
        value = value.encode("ascii") + b"\0"
        at = len(strings)
        require(at < 65536, "String parameter exceeds native direct index")
        strings.extend(value)
        return at

    def add_values(values):
        at = len(stream)
        stream.extend([*values, 0xFFFFFFFF])
        return at

    def rows(block, words):
        count = struct.unpack_from("<I", old[block], 4)[0]
        require(len(old[block]) == 8 + count * words * 4, "Invalid row stride")
        return [list(struct.unpack_from("<" + "I" * words, old[block], 8 + i * words * 4))
                for i in range(count)]

    actors, tools = rows(10, 16), rows(11, 19)
    for row in actors + tools:
        require(row[1] == tool_hash(string(row[0])), "Native tool hash mismatch")
    by_name = {string(row[0]): row for row in tools}
    require(ITEM not in by_name, "Tool already present")
    source = next(row for row in actors if string(row[0]) == "Megaphone")
    actor = source.copy()
    actor[0] = add_string("LiteralCrabMegaphone")
    actor[1] = tool_hash("LiteralCrabMegaphone")
    actor[2] = add_string("Tool_LiteralCrabMegaphone")
    require(string(source[2]) == "Tool_Megaphone", "Unexpected original actor")
    # Reuse the native gun input/upper-body context, not anybody's combo attacks.
    tool = by_name["ITEM_MIDWAYMANIAGUN"].copy()
    tool[0] = add_string(ITEM)
    tool[1] = tool_hash(ITEM)
    tool[3] = 0xFFFFFFFF  # Local story capability, no profile upgrade award.
    # Story tool in MU; the existing shared-tools option may widen this filter.
    tool[4] = add_values([add_string("mu_")])
    tool[5] = tool[0]
    tool[6] = add_values([len(actors)])
    tool[7] = 0xFFFFFFFF
    # Direct true, unlike hidden native abilities with per-avatar availability 0.
    tool[17] = tool[18] = by_name["ITEM_MIDWAYMANIAGUN"][17]
    require(string(tool[17]) == "1", "Unexpected tool availability parameter")
    # InitTools instantiates ONLY the indices in the matching actor profile.
    # A row in the master table alone never reaches the player's inventory.
    # Preserve old streams and insert the new item before Props/Abilities so
    # native contiguous-group discovery keeps the original default selections.
    profiles = rows(12, 2)
    changed_profiles = []
    for i, (key, start) in enumerate(profiles):
        name = string(key)
        if name not in BIPEDS:
            continue
        end = stream.index(0xFFFFFFFF, start)
        original_ids = stream[start:end]
        require(all(0 <= index < len(tools) for index in original_ids), "Invalid profile tool index")
        insert = 0
        while insert < len(original_ids) and string(tools[original_ids[insert]][2]) == "Items":
            insert += 1
        require(insert > 0 and all(string(tools[index][2]) != "Items" for index in original_ids[insert:]),
                "Unexpected native tool group order")
        ids = original_ids[:insert] + [len(tools)] + original_ids[insert:]
        require([index for index in ids if index != len(tools)] == original_ids, "Original profile order changed")
        struct.pack_into("<I", blocks[12], 12 + i * 8, add_values(ids))
        changed_profiles.append(name)
    require(set(changed_profiles) == BIPEDS and len(changed_profiles) == 26, "Incomplete biped profile coverage")
    blocks[0] = bytearray(struct.pack("<I", 4 + len(strings)) + strings)
    blocks[1] = bytearray(struct.pack("<I", 4 + 4 * len(stream)) + struct.pack("<" + "I" * len(stream), *stream))
    for block, row, count in [(10, actor, len(actors)), (11, tool, len(tools))]:
        blocks[block].extend(struct.pack("<" + "I" * len(row), *row))
        struct.pack_into("<I", blocks[block], 0, len(blocks[block]))
        struct.pack_into("<I", blocks[block], 4, count + 1)
    # Original rows, stream offsets, conditions and effects stay exact. Only
    # the 26 biped profile pointers move to additive inventory streams.
    for i, (before, after) in enumerate(zip(old, blocks)):
        if i in (0, 1):
            require(after[4:len(before)] == before[4:], "Original stream changed")
        elif i in (10, 11):
            require(after[8:len(before)] == before[8:], "Original rows changed")
        elif i == 12:
            require(after[:8] == before[:8], "Profile count changed")
            for j, (key, _) in enumerate(profiles):
                at = 8 + j * 8
                require(after[at:at + 4] == before[at:at + 4], "Actor profile mask changed")
                if string(key) not in BIPEDS:
                    require(after[at:at + 8] == before[at:at + 8], "Non-player profile changed")
        else:
            require(before == after, "Unrelated table changed")
    return b"".join(blocks), {"original_tools": len(tools), "original_actors": len(actors),
                              "new_tool_index": len(tools), "new_actor_index": len(actors),
                              "biped_inventory_profiles": changed_profiles,
                              "unchanged_other_profiles": len(profiles) - len(changed_profiles)}


def verify_inventories(original, changed):
    """Read serialized actor inventories as InitTools does, including group order."""
    def read(raw):
        blocks = split_blocks(raw)
        strings = blocks[0][4:]
        stream = struct.unpack("<" + "I" * ((len(blocks[1]) - 4) // 4), blocks[1][4:])

        def text(at):
            return strings[at:strings.index(0, at)].decode("ascii")

        count = struct.unpack_from("<I", blocks[11], 4)[0]
        groups = [text(struct.unpack_from("<I", blocks[11], 8 + i * 76 + 8)[0]) for i in range(count)]
        profiles = {}
        for i in range(struct.unpack_from("<I", blocks[12], 4)[0]):
            key, at = struct.unpack_from("<II", blocks[12], 8 + i * 8)
            ids = []
            while stream[at] != 0xFFFFFFFF:
                require(stream[at] < count, "Inventory references absent tool")
                ids.append(stream[at])
                at += 1
            profiles[text(key)] = ids
        return profiles, groups

    before, old_groups = read(original)
    after, groups = read(changed)
    require(before.keys() == after.keys(), "Actor inventory profiles changed")
    new_id = len(old_groups)
    require(groups == old_groups + ["Items"], "Tool categories changed")
    for name, ids in after.items():
        if name in BIPEDS:
            require(ids.count(new_id) == 1, "Missing/duplicate megaphone in inventory: " + name)
            require([i for i in ids if i != new_id] == before[name], "Existing inventory changed: " + name)
        else:
            require(ids == before[name], "Non-player inventory changed: " + name)
        # The native constructor discovers groups by consecutive category runs
        # and chooses each run's first item. Preserve both properties.
        def runs(indices, categories):
            result = []
            for index in indices:
                category = categories[index]
                if not result or result[-1][0] != category:
                    result.append((category, index))
            return result
        require(runs(ids, groups) == runs(before[name], old_groups), "Default/group order changed: " + name)
    return {"biped_inventories": len(BIPEDS), "unchanged_other_inventories": len(before) - len(BIPEDS),
            "serialized_membership_and_native_group_defaults": "PASS"}


DNA = '''DNA = {
 ActorScaleDeviation=0, ActorScaleMedian=100,
 ActorShutdownDistance=100, ActorShutdownEnabled=true,
 AgentGadgets={
  ActorTag={}, KinematicState={},
  Collision={AllowCompositing=true, CollisionResolutionMode="None", Shape="Authored",
   CollisionFlags={CategoryBits="NO_SUPPORT_CATEGORY | CAMERA_IGNORES_CATEGORY | PENETRABLE_CATEGORY",
    PositiveExcludeBits="NO_CATEGORY", PositiveIncludeBits="AI_CATEGORY | PLAYER_CATEGORY | ACTOR_CATEGORY"}},
  EntityInstance={ModelName="characters/Avatar_Tools/MU_Megaphone", RootNode=""},
  Geometry={DistanceAlphaFadeEnd=0, DistanceAlphaFadeStart=0},
  Identification={ActorTypeMask="Actor", DialogID="GN", LoadAudio=true, Name="LiteralCrabMegaphone"},
  Script={ScriptName="characters/LiteralCrab_Megaphone.sx"},
  StateBitmask={ActorStateMask="NONE"}
 }
}
'''


def build_scare_script(original, reaction):
    """Use the original avatar inventory lifecycle, not a prop's gun inventory.

    Keep the native function/label tables and every code address. Replaced
    function tails contain unreachable RETURN instructions, never extra calls.
    Animation and mission acceptance remain separate from this owner correction.
    """
    require(sha(original) == SCRIPT_SHA, "Unsupported original LR shotgun script")
    require(sha(reaction) == REACTION_SCRIPT_SHA, "Unsupported original reaction script")
    require(original[:12] == b"SLNG\x0c\x10\0\0\x78\x56\x34\x12", "Unsupported script VM format")
    require(struct.unpack_from("<I", original, 0x18)[0] == 582, "Unexpected script code size")
    code_at = 0x40 + struct.unpack_from("<I", original, 0x40)[0] + 1
    require(code_at == 0x68, "Unexpected script code offset")
    script = bytearray(original)

    def replace_words(first, stop, words):
        require(first < stop <= 582 and len(words) <= stop - first, "Invalid replacement function")
        words = words + [0x2D] * (stop - first - len(words))
        script[code_at + first * 2:code_at + stop * 2] = struct.pack("<" + "H" * len(words), *words)

    # Original Fired passes owner to StartWeapon and ShootShotgun. StartWeapon
    # adds/activates a dynamic slot on that avatar; EndWeapon releases/removes it
    # after the original 0.25s window. Preserve that lifecycle and slot handling.
    # ShootShotgun: consume its actor argument, fire the owned slot once, then
    # mark the magazine empty. Do not lock the muzzle to the carried prop's hand,
    # aim a body-relative box at a gun target, or load muzzle choreography.
    # Play the scare AV explicitly: override motion playback alone does not
    # dispatch the normal combo metadata events on visiting characters.
    replace_words(0x61, 0xAE, [0x09, 0x0E, 0x00, 0x0E, 0x00, 0x05,
                              0x2C, 0x08,
                              # Original scare reactions also send ScareAttack
                              # with float 0 to their avatar. Native wrapper
                              # pops receiver, typeless data, then message name.
                              0x07, 0x1B, 0x06, 0x00, 0x0E, 0x2C, 0x02,
                              # Separate visual-only message; ScareAttack above
                              # and its weapon source/lifetime are unchanged.
                              0x07, 0x08, 0x06, 0x00, 0x0E, 0x2C, 0x02,
                              0x05, 0x09, 0x02,
                              # Original SfxPlay stack contract: one name,
                              # no result. Slot 9 replaces the unused Clamp.
                              0x07, 0x07, 0x2C, 0x09,
                              # Original LoadChoreography/Drive sequence, with
                              # the avatar's Cntrl_Main instead of a gun muzzle.
                              0x00, 0x0E, 0x07, 0x13, 0x05, 0x2C, 0x0A,
                              0x09, 0x12,
                              0x07, 0x15, 0x07, 0x16, 0x00, 0x12,
                              0x00, 0x0E, 0x05, 0x2C, 0x0B, 0x2D])
    # One-second native input cooldown, with no gun reload event or sound.
    replace_words(0x17, 0x3A, [0x00, 0x00, 0x07, 0x03, 0x05, 0x2C, 0x00,
                              0x0106, 0x33, 0x36, 0x0105, 0x09, 0x02,
                              0x00, 0x00, 0x07, 0x03, 0x0105, 0x2C, 0x00,
                              0x2A, 0x12])
    # Skip the original shotgun equip sound, preserving the handler's Wait result.
    replace_words(0x226, 0x22A, [0x2A, 0x22A])
    # Reuse the now-unused SfxPlay binding for the original message API. Its
    # hash and stack contract come verbatim from the supported reaction script.
    # Two discarded debug literals provide space without moving any table.
    def rel(raw, at):
        return at + struct.unpack_from("<I", raw, at)[0] + 1

    strings = rel(original, 0x64)
    def replace_literal(index, expected, value):
        target = rel(original, strings + index * 4)
        expected = expected.encode("ascii") + b"\0"
        value = value.encode("ascii") + b"\0"
        require(original[target:target + len(expected)] == expected and len(value) <= len(expected), "Unexpected debug literal")
        script[target:target + len(expected)] = value.ljust(len(expected), b"\0")
        return target

    message_name = replace_literal(10, "LR_Shotgun.sla: Fired!", "SendMessageToActor")
    replace_literal(27, "StartWeapon: Pulling trigger on: ", "ScareAttack")
    replace_literal(8, "SfxPlay", "LCRPose")
    native = rel(original, 0x50) + 2 * 16
    donor = rel(reaction, 0x50) + 155 * 16
    require(original[rel(original, native):].startswith(b"SfxPlay\0"), "Unexpected native sound binding")
    require(reaction[rel(reaction, donor):].startswith(b"SendMessageToActor\0"), "Unexpected native message binding")
    struct.pack_into("<I", script, native, message_name - native - 1)
    script[native + 4:native + 16] = reaction[donor + 4:donor + 16]
    # Keep every table/code address fixed. The discarded long assertion text
    # provides storage for two strings; its old diagnostic now names SfxPlay.
    sound_name = replace_literal(25, "LR_Shotgun.sla: Could not add weapon to inventory!",
                                 "SfxPlay\0mike_scare_yell")
    struct.pack_into("<I", script, strings + 7 * 4, sound_name + 8 - (strings + 7 * 4) - 1)
    sound_binding = rel(original, 0x50) + 9 * 16
    require(original[rel(original, sound_binding):].startswith(b"Clamp\0"), "Unexpected spare native binding")
    struct.pack_into("<I", script, sound_binding, sound_name - sound_binding - 1)
    script[sound_binding + 4:sound_binding + 16] = original[native + 4:native + 16]
    replace_literal(19, "WpnFxShotGunA_m", "mu_scare_green")
    replace_literal(21, "ANC_WpnFxShotGunA_m_00", "Layout")
    replace_literal(22, "ControlParticle", "Cntrl_Main")
    for before, after in [(b"ShotgunShot\0", b"LCRMUScare\0\0"),
                          (b"lr_shotgun\0", b"lc_muscare\0")]:
        require(len(before) == len(after) and script.count(before) == 1, "Ambiguous script constant")
        script = script.replace(before, after)
    require(len(script) == len(original), "Native script layout changed")
    require(script[:0x68] == original[:0x68], "Native script header changed")
    return bytes(script)


def build(assets, output):
    require(not output.exists(), "Output must be a new directory")
    with zipfile.ZipFile(assets / "startup5.zip") as archive:
        raw_table = archive.read("var/toolset/toolset.bin")
    table, info = build_table(raw_table)
    inventory_check = verify_inventories(raw_table, table)
    with zipfile.ZipFile(assets / "characters/characters.zip") as archive:
        original = archive.read("lr_shotgun.sx")
        reaction = archive.read("reaction.sx")
    script = build_scare_script(original, reaction)
    mod = output / "mods" / MOD
    files = {"assets/var/toolset/toolset.bin": table,
             "assets/characters/LiteralCrab_Megaphone.sx": script,
             "assets/characters/LiteralCrab_Megaphone.dnax": DNA.encode("ascii"),
             "mod.ini": b"[Mod]\nName=Experimental MU scare megaphone\nEnabled=1\n"}
    for name, raw in files.items():
        target = mod / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
    report = {"status": "EXPERIMENTAL_NOT_GAMEPLAY_ACCEPTED", **info,
              "input_sha256": {"toolset.bin": sha(raw_table), "lr_shotgun.sx": sha(original), "reaction.sx": sha(reaction)},
              "files": {name: sha(raw) for name, raw in files.items()},
              "original_rows_and_unrelated_blocks_preserved": True,
              "native_script_layout_preserved": True,
              "inventory_verification": inventory_check,
              "avatar_owned_weapon_lifecycle": True,
              "native_scare_message_on_avatar": True,
              "private_pose_message": "LCRPose",
              "gun_muzzle_choreography_and_sounds_removed": True,
              "explicit_scare_audio": "mike_scare_yell",
              "explicit_scare_choreography": "mu_scare_green",
              "scare_fx_anchor": "Layout",
              "scare_fx_avatar_control": "Cntrl_Main",
              "limits": ["Scare target/mission accounting needs gameplay validation.",
                         "Mike gesture requires the matching ASI and updated playset animation provider; rendered fit is not certified.",
                         "Tool uses a Mike inventory icon; localized label requires the matching ASI.",
                         "Other toolset.bin replacement mods conflict; first active asset wins."]}
    (output / "build.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(json.dumps(build(args.assets.resolve(), args.output.resolve()), indent=2))
