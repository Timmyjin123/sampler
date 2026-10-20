#!/usr/bin/env python3
"""Generates GrainPad.xcodeproj from the files on disk.

Run from the grainpad/ folder after adding or removing source files:
    python3 tools/gen_xcodeproj.py
IDs are derived from file paths, so regenerating gives a stable project file.
"""
import hashlib
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = "GrainPad"
PROJ = os.path.join(ROOT, "GrainPad.xcodeproj")
BUNDLE_ID = "com.timmyjin123.grainpad"

FILE_TYPES = {
    ".swift": "sourcecode.swift",
    ".mm": "sourcecode.cpp.objcpp",
    ".cpp": "sourcecode.cpp.cpp",
    ".h": "sourcecode.c.h",
    ".hpp": "sourcecode.cpp.h",
    ".plist": "text.plist.xml",
    ".xcassets": "folder.assetcatalog",
}
SOURCES = {".swift", ".mm", ".cpp"}
RESOURCES = {".xcassets"}


def uid(*parts):
    return hashlib.sha1("/".join(parts).encode()).hexdigest()[:24].upper()


def q(s):
    safe = all(c.isalnum() or c in "._/$" for c in s)
    return s if safe and s else '"' + s.replace('"', '\\"') + '"'


def collect():
    """Returns {group_path: [entries]} where entries are files or subgroups."""
    tree = {}

    def walk(rel):
        full = os.path.join(ROOT, rel)
        entries = []
        for name in sorted(os.listdir(full)):
            if name.startswith("."):
                continue
            p = os.path.join(rel, name)
            ext = os.path.splitext(name)[1]
            if os.path.isdir(os.path.join(ROOT, p)) and ext != ".xcassets":
                walk(p)
                entries.append(("group", p))
            elif ext in FILE_TYPES:
                entries.append(("file", p))
        tree[rel] = entries

    walk(SRC)
    return tree


def main():
    tree = collect()
    objs = {}   # id -> text

    files = [p for entries in tree.values() for kind, p in entries if kind == "file"]
    build_sources, build_resources = [], []
    for p in files:
        ext = os.path.splitext(p)[1]
        fid = uid("ref", p)
        objs[fid] = (f"{fid} /* {os.path.basename(p)} */ = {{isa = PBXFileReference; lastKnownFileType = {FILE_TYPES[ext]}; "
                     f"path = {q(os.path.basename(p))}; sourceTree = \"<group>\"; }};")
        if ext in SOURCES or ext in RESOURCES:
            bid = uid("build", p)
            phase = "Sources" if ext in SOURCES else "Resources"
            objs[bid] = (f"{bid} /* {os.path.basename(p)} in {phase} */ = {{isa = PBXBuildFile; fileRef = {fid} /* {os.path.basename(p)} */; }};")
            (build_sources if ext in SOURCES else build_resources).append((bid, os.path.basename(p)))

    # system frameworks
    fw_build = []
    for fw in ["AVFoundation", "AudioToolbox"]:
        fid = uid("fw", fw)
        bid = uid("fwbuild", fw)
        objs[fid] = (f"{fid} /* {fw}.framework */ = {{isa = PBXFileReference; lastKnownFileType = wrapper.framework; "
                     f"name = {fw}.framework; path = System/Library/Frameworks/{fw}.framework; sourceTree = SDKROOT; }};")
        objs[bid] = f"{bid} /* {fw}.framework in Frameworks */ = {{isa = PBXBuildFile; fileRef = {fid} /* {fw}.framework */; }};"
        fw_build.append((bid, fw, fid))

    # groups
    for rel, entries in tree.items():
        gid = uid("group", rel)
        children = []
        for kind, p in entries:
            cid = uid("group", p) if kind == "group" else uid("ref", p)
            children.append(f"\t\t\t\t{cid} /* {os.path.basename(p)} */,")
        objs[gid] = (f"{gid} /* {os.path.basename(rel)} */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n" + "\n".join(children) +
                     f"\n\t\t\t);\n\t\t\tpath = {q(os.path.basename(rel))};\n\t\t\tsourceTree = \"<group>\";\n\t\t}};")

    product = uid("product")
    objs[product] = (f"{product} /* GrainPad.app */ = {{isa = PBXFileReference; explicitFileType = wrapper.application; "
                     f"includeInIndex = 0; path = GrainPad.app; sourceTree = BUILT_PRODUCTS_DIR; }};")
    products_group = uid("group", "Products")
    objs[products_group] = (f"{products_group} /* Products */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n\t\t\t\t{product} /* GrainPad.app */,\n"
                            f"\t\t\t);\n\t\t\tname = Products;\n\t\t\tsourceTree = \"<group>\";\n\t\t}};")
    fw_group = uid("group", "Frameworks")
    objs[fw_group] = (f"{fw_group} /* Frameworks */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n" +
                      "\n".join(f"\t\t\t\t{fid} /* {fw}.framework */," for _, fw, fid in fw_build) +
                      f"\n\t\t\t);\n\t\t\tname = Frameworks;\n\t\t\tsourceTree = \"<group>\";\n\t\t}};")
    main_group = uid("group", "<main>")
    objs[main_group] = (f"{main_group} = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n\t\t\t\t{uid('group', SRC)} /* GrainPad */,\n"
                        f"\t\t\t\t{fw_group} /* Frameworks */,\n\t\t\t\t{products_group} /* Products */,\n\t\t\t);\n\t\t\tsourceTree = \"<group>\";\n\t\t}};")

    # build phases
    sources_phase = uid("phase", "sources")
    objs[sources_phase] = (f"{sources_phase} /* Sources */ = {{\n\t\t\tisa = PBXSourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n" +
                           "\n".join(f"\t\t\t\t{b} /* {n} in Sources */," for b, n in build_sources) +
                           "\n\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};")
    res_phase = uid("phase", "resources")
    objs[res_phase] = (f"{res_phase} /* Resources */ = {{\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n" +
                       "\n".join(f"\t\t\t\t{b} /* {n} in Resources */," for b, n in build_resources) +
                       "\n\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};")
    fw_phase = uid("phase", "frameworks")
    objs[fw_phase] = (f"{fw_phase} /* Frameworks */ = {{\n\t\t\tisa = PBXFrameworksBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n" +
                      "\n".join(f"\t\t\t\t{b} /* {fw}.framework in Frameworks */," for b, fw, _ in fw_build) +
                      "\n\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};")

    # build settings
    def settings_block(d):
        lines = []
        for k in sorted(d):
            v = d[k]
            if isinstance(v, list):
                lines.append(f"\t\t\t\t{k} = (\n" + "\n".join(f"\t\t\t\t\t{q(x)}," for x in v) + "\n\t\t\t\t);")
            else:
                lines.append(f"\t\t\t\t{k} = {q(str(v))};")
        return "\n".join(lines)

    common_proj = {
        "ALWAYS_SEARCH_USER_PATHS": "NO",
        "CLANG_ANALYZER_NONNULL": "YES",
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "CLANG_CXX_LIBRARY": "libc++",
        "CLANG_ENABLE_MODULES": "YES",
        "CLANG_ENABLE_OBJC_ARC": "YES",
        "CLANG_WARN_BOOL_CONVERSION": "YES",
        "CLANG_WARN_INFINITE_RECURSION": "YES",
        "CLANG_WARN_UNREACHABLE_CODE": "YES",
        "COPY_PHASE_STRIP": "NO",
        "ENABLE_STRICT_OBJC_MSGSEND": "YES",
        "ENABLE_USER_SCRIPT_SANDBOXING": "YES",
        "GCC_C_LANGUAGE_STANDARD": "gnu17",
        "GCC_NO_COMMON_BLOCKS": "YES",
        "IPHONEOS_DEPLOYMENT_TARGET": "16.0",
        "SDKROOT": "iphoneos",
    }
    proj_debug = dict(common_proj, **{
        "DEBUG_INFORMATION_FORMAT": "dwarf",
        "ENABLE_TESTABILITY": "YES",
        "GCC_PREPROCESSOR_DEFINITIONS": ["DEBUG=1", "$(inherited)"],
        "ONLY_ACTIVE_ARCH": "YES",
        "SWIFT_ACTIVE_COMPILATION_CONDITIONS": "DEBUG",
        "SWIFT_OPTIMIZATION_LEVEL": "-Onone",
    })
    proj_release = dict(common_proj, **{
        "DEBUG_INFORMATION_FORMAT": "dwarf-with-dsym",
        "SWIFT_COMPILATION_MODE": "wholemodule",
        "SWIFT_OPTIMIZATION_LEVEL": "-O",
        "VALIDATE_PRODUCT": "YES",
    })
    target_common = {
        "ASSETCATALOG_COMPILER_APPICON_NAME": "AppIcon",
        "ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME": "AccentColor",
        "CODE_SIGN_STYLE": "Automatic",
        "CURRENT_PROJECT_VERSION": "1",
        "DEVELOPMENT_TEAM": "",
        "ENABLE_PREVIEWS": "YES",
        # the audio engine must be optimized even in Debug, or it can glitch on device
        "GCC_OPTIMIZATION_LEVEL": "3",
        "GENERATE_INFOPLIST_FILE": "YES",
        "HEADER_SEARCH_PATHS": ["$(inherited)", "$(SRCROOT)/GrainPad/Engine", "$(SRCROOT)/GrainPad/Audio"],
        "INFOPLIST_FILE": "GrainPad/Info.plist",
        "INFOPLIST_KEY_CFBundleDisplayName": "GrainPad",
        "INFOPLIST_KEY_LSApplicationCategoryType": "public.app-category.music",
        "INFOPLIST_KEY_NSMicrophoneUsageDescription": "GrainPad records sounds from the microphone into your pads.",
        "INFOPLIST_KEY_UILaunchScreen_Generation": "YES",
        "INFOPLIST_KEY_UIStatusBarStyle": "UIStatusBarStyleLightContent",
        "INFOPLIST_KEY_UISupportedInterfaceOrientations_iPad": "UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight",
        "INFOPLIST_KEY_UISupportedInterfaceOrientations_iPhone": "UIInterfaceOrientationPortrait",
        "LD_RUNPATH_SEARCH_PATHS": ["$(inherited)", "@executable_path/Frameworks"],
        "MARKETING_VERSION": "0.1",
        "PRODUCT_BUNDLE_IDENTIFIER": BUNDLE_ID,
        "PRODUCT_NAME": "$(TARGET_NAME)",
        "SWIFT_EMIT_LOC_STRINGS": "YES",
        "SWIFT_OBJC_BRIDGING_HEADER": "GrainPad/GrainPad-Bridging-Header.h",
        "SWIFT_VERSION": "5.0",
        "TARGETED_DEVICE_FAMILY": "1,2",
    }

    cfgs = {}
    for scope, debug, release in [("project", proj_debug, proj_release), ("target", target_common, target_common)]:
        ids = []
        for name, st in [("Debug", debug), ("Release", release)]:
            cid = uid("config", scope, name)
            objs[cid] = (f"{cid} /* {name} */ = {{\n\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {{\n" + settings_block(st) +
                         f"\n\t\t\t}};\n\t\t\tname = {name};\n\t\t}};")
            ids.append((cid, name))
        lid = uid("configlist", scope)
        owner = "PBXProject \"GrainPad\"" if scope == "project" else "PBXNativeTarget \"GrainPad\""
        objs[lid] = (f"{lid} /* Build configuration list for {owner} */ = {{\n\t\t\tisa = XCConfigurationList;\n\t\t\tbuildConfigurations = (\n" +
                     "\n".join(f"\t\t\t\t{c} /* {n} */," for c, n in ids) +
                     "\n\t\t\t);\n\t\t\tdefaultConfigurationIsVisible = 0;\n\t\t\tdefaultConfigurationName = Release;\n\t\t};")
        cfgs[scope] = lid

    target = uid("target")
    objs[target] = (f"{target} /* GrainPad */ = {{\n\t\t\tisa = PBXNativeTarget;\n\t\t\tbuildConfigurationList = {cfgs['target']};\n"
                    f"\t\t\tbuildPhases = (\n\t\t\t\t{sources_phase} /* Sources */,\n\t\t\t\t{fw_phase} /* Frameworks */,\n\t\t\t\t{res_phase} /* Resources */,\n\t\t\t);\n"
                    f"\t\t\tbuildRules = (\n\t\t\t);\n\t\t\tdependencies = (\n\t\t\t);\n\t\t\tname = GrainPad;\n\t\t\tproductName = GrainPad;\n"
                    f"\t\t\tproductReference = {product} /* GrainPad.app */;\n\t\t\tproductType = \"com.apple.product-type.application\";\n\t\t}};")
    project = uid("project")
    objs[project] = (f"{project} /* Project object */ = {{\n\t\t\tisa = PBXProject;\n\t\t\tattributes = {{\n\t\t\t\tBuildIndependentTargetsInParallel = 1;\n"
                     f"\t\t\t\tLastSwiftUpdateCheck = 1600;\n\t\t\t\tLastUpgradeCheck = 1600;\n\t\t\t\tTargetAttributes = {{\n\t\t\t\t\t{target} = {{\n"
                     f"\t\t\t\t\t\tCreatedOnToolsVersion = 16.0;\n\t\t\t\t\t}};\n\t\t\t\t}};\n\t\t\t}};\n\t\t\tbuildConfigurationList = {cfgs['project']};\n"
                     f"\t\t\tcompatibilityVersion = \"Xcode 14.0\";\n\t\t\tdevelopmentRegion = en;\n\t\t\thasScannedForEncodings = 0;\n"
                     f"\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t\tBase,\n\t\t\t);\n\t\t\tmainGroup = {main_group};\n"
                     f"\t\t\tproductRefGroup = {products_group} /* Products */;\n\t\t\tprojectDirPath = \"\";\n\t\t\tprojectRoot = \"\";\n"
                     f"\t\t\ttargets = (\n\t\t\t\t{target} /* GrainPad */,\n\t\t\t);\n\t\t}};")

    # group objects by isa, like Xcode does
    sections = {}
    for oid, text in objs.items():
        isa = text.split("isa = ")[1].split(";")[0]
        sections.setdefault(isa, []).append(text)
    out = ["// !$*UTF8*$!", "{", "\tarchiveVersion = 1;", "\tclasses = {", "\t};", "\tobjectVersion = 56;", "\tobjects = {"]
    for isa in sorted(sections):
        out.append(f"\n/* Begin {isa} section */")
        for text in sorted(sections[isa]):
            out.append("\t\t" + text)
        out.append(f"/* End {isa} section */")
    out += ["\t};", f"\trootObject = {project} /* Project object */;", "}", ""]

    os.makedirs(PROJ, exist_ok=True)
    with open(os.path.join(PROJ, "project.pbxproj"), "w") as f:
        f.write("\n".join(out))

    ws = os.path.join(PROJ, "project.xcworkspace")
    os.makedirs(ws, exist_ok=True)
    with open(os.path.join(ws, "contents.xcworkspacedata"), "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<Workspace\n   version = "1.0">\n   <FileRef\n      location = "self:">\n   </FileRef>\n</Workspace>\n')

    schemes = os.path.join(PROJ, "xcshareddata", "xcschemes")
    os.makedirs(schemes, exist_ok=True)
    ref = (f'<BuildableReference BuildableIdentifier = "primary" BlueprintIdentifier = "{target}" '
           f'BuildableName = "GrainPad.app" BlueprintName = "GrainPad" ReferencedContainer = "container:GrainPad.xcodeproj">\n'
           f'            </BuildableReference>')
    with open(os.path.join(schemes, "GrainPad.xcscheme"), "w") as f:
        f.write(f'''<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion = "1600" version = "1.7">
   <BuildAction parallelizeBuildables = "YES" buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting = "YES" buildForRunning = "YES" buildForProfiling = "YES" buildForArchiving = "YES" buildForAnalyzing = "YES">
            {ref}
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <TestAction buildConfiguration = "Debug" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" shouldUseLaunchSchemeArgsEnv = "YES">
   </TestAction>
   <LaunchAction buildConfiguration = "Debug" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle = "0" useCustomWorkingDirectory = "NO" ignoresPersistentStateOnLaunch = "NO" debugDocumentVersioning = "YES" debugServiceExtension = "internal" allowLocationSimulation = "YES">
      <BuildableProductRunnable runnableDebuggingMode = "0">
            {ref}
      </BuildableProductRunnable>
   </LaunchAction>
   <ProfileAction buildConfiguration = "Release" shouldUseLaunchSchemeArgsEnv = "YES" savedToolIdentifier = "" useCustomWorkingDirectory = "NO" debugDocumentVersioning = "YES">
      <BuildableProductRunnable runnableDebuggingMode = "0">
            {ref}
      </BuildableProductRunnable>
   </ProfileAction>
   <AnalyzeAction buildConfiguration = "Debug">
   </AnalyzeAction>
   <ArchiveAction buildConfiguration = "Release" revealArchiveInOrganizer = "YES">
   </ArchiveAction>
</Scheme>
''')
    print(f"wrote {PROJ} ({len(build_sources)} sources, {len(build_resources)} resources)")


if __name__ == "__main__":
    main()
