#!/usr/bin/env python3
"""
Applies the Online Co-op engine changes to a Sonic 3 A.I.R. source tree.

Usage:  python apply_coop.py <path to sonic3air source root>

Designed to work across game versions: instead of a line-based patch, every edit finds a
short anchor text in the original file and inserts the co-op lines next to it. Running it
twice is safe (already-applied edits are skipped). If an anchor isn't found (because a game
update changed that spot), it stops with a clear message saying which file and anchor.
"""

import os
import shutil
import sys

COOP_DIR = os.path.dirname(os.path.abspath(__file__))
MARK = "CoopNet addition"


class AnchorMissing(Exception):
	pass


def edit_file(root, rel_path, edits):
	path = os.path.join(root, rel_path)
	with open(path, "r", encoding="utf-8", errors="surrogateescape", newline="") as f:
		text = f.read()
	nl = "\r\n" if "\r\n" in text else "\n"

	changed = False
	for anchor, insert, where, marker in edits:
		insert_n = insert.replace("\n", nl)
		if marker in text:
			continue		# Already applied
		anchor_n = anchor.replace("\n", nl)
		count = text.count(anchor_n)
		if count != 1:
			raise AnchorMissing(f"{rel_path}: expected to find this text exactly once, found it {count} times:\n{anchor}")
		if where == "after":
			text = text.replace(anchor_n, anchor_n + insert_n, 1)
		else:
			text = text.replace(anchor_n, insert_n + anchor_n, 1)
		changed = True

	if changed:
		with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
			f.write(text)
	print(("  patched   " if changed else "  unchanged ") + rel_path)


def main():
	if len(sys.argv) != 2:
		print(__doc__)
		return 2
	root = os.path.abspath(sys.argv[1])
	engine = os.path.join("Oxygen", "oxygenengine")
	src = os.path.join(engine, "source", "oxygen")

	print("Applying Online Co-op to: " + root)

	# 1. New files
	dest_dir = os.path.join(root, src, "network", "coopnet")
	os.makedirs(dest_dir, exist_ok=True)
	for name in ("CoopNet.h", "CoopNet.cpp"):
		shutil.copyfile(os.path.join(COOP_DIR, "engine", name), os.path.join(dest_dir, name))
		print("  copied    " + os.path.join(src, "network", "coopnet", name))

	# 2. Keep the connection running every frame
	edit_file(root, os.path.join(src, "application", "Application.cpp"), [
		('#include "oxygen/simulation/Simulation.h"\n',
		 '#include "oxygen/network/coopnet/CoopNet.h"\t// CoopNet addition\n', "after", '#include "oxygen/network/coopnet/CoopNet.h"'),
		('\tEngineServerClient::instance().updateClient(timeElapsed);\n',
		 '\tCoopNet::instance().update(timeElapsed);\t// CoopNet addition\n', "after", 'CoopNet::instance().update('),
	])

	# 3. Make the "Net.*" functions available to scripts
	edit_file(root, os.path.join(src, "simulation", "bindings", "LemonScriptBindings.cpp"), [
		('#include <lemon/runtime/Runtime.h>\n',
		 '#include "oxygen/network/coopnet/CoopNet.h"\t// CoopNet addition\n', "after", '#include "oxygen/network/coopnet/CoopNet.h"'),
		('\tEngineMain::getDelegate().registerScriptBindings(module);\n',
		 '\t// CoopNet addition: online co-op messaging for script mods\n\tCoopNet::registerScriptBindings(module);\n\n', "before", 'CoopNet::registerScriptBindings('),
	])

	# 4. Sprite listener, so each game can mirror exactly what it draws for its own character
	edit_file(root, os.path.join(src, "rendering", "parts", "SpriteManager.h"), [
		('\tusing Space = SpacesManager::Space;\n',
		 '\n\t// CoopNet addition: optional listener that gets told about every tagged custom sprite (used to mirror characters to other players)\n'
		 '\tstruct SpriteListener\n\t{\n'
		 '\t\tvirtual void onSpriteFrameStart() = 0;\n'
		 '\t\tvirtual void onTaggedCustomSprite(uint64 key, const Vec2i& screenPosition, uint16 atex, uint8 flags, uint16 renderQueue, const Color& tintColor, const Transform2D& transformation, uint64 spriteTag, const Vec2i& taggedPosition) = 0;\n'
		 '\t};\n\tstatic inline SpriteListener* mSpriteListener = nullptr;\n', "after", 'struct SpriteListener'),
	])
	edit_file(root, os.path.join(src, "rendering", "parts", "SpriteManager.cpp"), [
		('\tmTaggedSpritesLastFrame.swap(mTaggedSpritesThisFrame);\n\tmTaggedSpritesThisFrame.clear();\n',
		 '\n\tif (nullptr != mSpriteListener)\t// CoopNet addition\n\t\tmSpriteListener->onSpriteFrameStart();\n', "after", 'mSpriteListener->onSpriteFrameStart()'),
		('\t// For smoother rotation',
		 '\t// CoopNet addition\n'
		 '\tif (nullptr != mSpriteListener && mSpriteTag != 0)\n\t{\n'
		 '\t\tconst Vec2i screenPosition = (sprite.mCoordinatesSpace == Space::WORLD) ? (position - mSpacesManager.getWorldSpaceOffset()) : position;\n'
		 '\t\tmSpriteListener->onTaggedCustomSprite(key, screenPosition, atex, flags & 0x40, renderQueue, tintColor, sprite.mTransformation, mSpriteTag, mTaggedSpritePosition);\n'
		 '\t}\n\n', "before", 'mSpriteListener->onTaggedCustomSprite(key,'),
		('\t\tif (data.mSpriteTag != 0)\n\t\t{\n\t\t\tconst auto it = mTaggedSpritesLastFrame.find(data.mSpriteTag);',
		 '\t\t// CoopNet addition\n'
		 '\t\tif (nullptr != mSpriteListener && data.mSpriteTag != 0)\n\t\t{\n'
		 '\t\t\tconst Vec2i screenPosition = (data.mCoordinatesSpace == Space::WORLD) ? (data.mPosition - mSpacesManager.getWorldSpaceOffset()) : data.mPosition;\n'
		 '\t\t\tmSpriteListener->onTaggedCustomSprite(data.mKey, screenPosition, data.mAtex, data.mPriorityFlag ? 0x40 : 0, data.mRenderQueue, sprite.mTintColor, sprite.mTransformation, data.mSpriteTag, data.mTaggedSpritePosition);\n'
		 '\t\t}\n\n', "before", 'mSpriteListener->onTaggedCustomSprite(data.mKey,'),
	])

	# 5. Visual Studio project (CMake builds pick up new files automatically)
	vs = os.path.join(engine, "build", "_vstudio")
	edit_file(root, os.path.join(vs, "oxygen.vcxproj"), [
		('    <ClCompile Include="..\\..\\source\\oxygen\\network\\netplay\\NetplayHost.cpp" />\n',
		 '    <ClCompile Include="..\\..\\source\\oxygen\\network\\coopnet\\CoopNet.cpp" />\n', "before", 'coopnet\\CoopNet.cpp'),
		('    <ClInclude Include="..\\..\\source\\oxygen\\network\\netplay\\NetplayHost.h" />\n',
		 '    <ClInclude Include="..\\..\\source\\oxygen\\network\\coopnet\\CoopNet.h" />\n', "before", 'coopnet\\CoopNet.h'),
	])
	edit_file(root, os.path.join(vs, "oxygen.vcxproj.filters"), [
		('    <ClCompile Include="..\\..\\source\\oxygen\\network\\netplay\\NetplayHost.cpp">\n',
		 '    <ClCompile Include="..\\..\\source\\oxygen\\network\\coopnet\\CoopNet.cpp">\n      <Filter>network</Filter>\n    </ClCompile>\n', "before", 'coopnet\\CoopNet.cpp'),
		('    <ClInclude Include="..\\..\\source\\oxygen\\network\\netplay\\NetplayHost.h">\n',
		 '    <ClInclude Include="..\\..\\source\\oxygen\\network\\coopnet\\CoopNet.h">\n      <Filter>network</Filter>\n    </ClInclude>\n', "before", 'coopnet\\CoopNet.h'),
	])

	print("Done.")
	return 0


if __name__ == "__main__":
	try:
		sys.exit(main())
	except AnchorMissing as e:
		print("\nERROR: This game version changed a spot the co-op changes rely on.\n" + str(e))
		sys.exit(1)
