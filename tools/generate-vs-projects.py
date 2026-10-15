"""按实际源码目录生成可迁移的 VS2022 项目；CMake 仍是跨平台构建入口。"""
from pathlib import Path
from uuid import uuid5, NAMESPACE_URL
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "visual-studio"
PROJECTS = ["ningzhou_web", "nzcore", "ningzhou", "ningzhou_tests", "ningzhou_battle_lab"]
GUIDS = {name: "{" + str(uuid5(NAMESPACE_URL, "ningzhou-dream/" + name)).upper() + "}" for name in PROJECTS}


# 用途：生成项目相对路径；流程：便携VS项目生成；用法：传仓库内文件；返回：相对于visual-studio的Windows路径。
def relative(file):
    return "..\\" + str(file.relative_to(ROOT)).replace("/", "\\")


# 用途：构建与本地CMake一致的项目文件列表；流程：解决方案同步；用法：传项目名；返回：编译/头文件/文档项目项。
def items(name):
    if name == "nzcore":
        files = list((ROOT / "src/core").rglob("*.cpp")) + list((ROOT / "src/core").rglob("*.h"))
        files += list((ROOT / "src/core").rglob("README.md")) + list((ROOT / "data").glob("*.h")) + list((ROOT / "docs").glob("*.md"))
    elif name == "ningzhou_tests":
        files = list((ROOT / "tests").glob("*.cpp")) + list((ROOT / "tests").glob("*.h"))
    elif name == "ningzhou":
        files = [ROOT / "src/ui.cpp"]
    else:
        files = [ROOT / ("src/battle_lab_main.cpp" if name == "ningzhou_battle_lab" else "src/web_main.cpp")]
        files += [file for file in (ROOT / "web").rglob("*") if file.suffix in {".mjs", ".css", ".html"}]
    result = []
    for file in sorted(files):
        parts = file.relative_to(ROOT).parts
        if parts[:2] == ("src", "core"):
            group = "Backend" + ("\\" + "\\".join(parts[2:-1]) if len(parts) > 3 else "")
        elif parts[0] == "web":
            group = "Web UI" + ("\\" + "\\".join(parts[1:-1]) if len(parts) > 2 else "")
        elif parts[0] == "docs":
            group = "Documentation"
        elif parts[0] == "data":
            group = "Content Tables"
        else:
            group = "Tests" if name == "ningzhou_tests" else "Entry"
        result.append(("ClCompile" if file.suffix == ".cpp" else "ClInclude" if file.suffix == ".h" else "None", relative(file), group))
    return result


# 用途：写入稳定UTF8项目文件；流程：VS入口生成；用法：传文件名与内容；返回：无，避免绝对机器路径进入仓库。
def write(name, text):
    (DEST / name).write_text(text, encoding="utf-8")


# 用途：生成解决方案、项目和目录筛选；流程：上传源码前同步；用法：直接执行该脚本；返回：无，输出visual-studio入口。
def generate():
    DEST.mkdir(exist_ok=True)
    common = '''<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <OutDir>$(MSBuildThisFileDirectory)..\\build-vs-share\\$(Configuration)\\</OutDir>
    <IntDir>$(MSBuildThisFileDirectory)..\\build-vs-share\\obj\\$(ProjectName)\\$(Configuration)\\</IntDir>
    <TargetName>$(ProjectName)</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$(MSBuildThisFileDirectory)..\\src;$(MSBuildThisFileDirectory)..;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <LanguageStandard>stdcpp20</LanguageStandard><WarningLevel>Level4</WarningLevel><ConformanceMode>true</ConformanceMode>
      <AdditionalOptions>/utf-8 /Zc:__cplusplus %(AdditionalOptions)</AdditionalOptions>
      <PreprocessorDefinitions>NOMINMAX;_CRT_SECURE_NO_WARNINGS;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <PreprocessorDefinitions Condition="'$(Configuration)'=='Debug'">NZ_DEBUG=1;NOMINMAX;_CRT_SECURE_NO_WARNINGS;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
    <Link><AdditionalDependencies>ws2_32.lib;%(AdditionalDependencies)</AdditionalDependencies></Link>
  </ItemDefinitionGroup>
</Project>
'''
    write("ningzhou.props", common)
    for name in PROJECTS:
        entries = items(name)
        xml = ['<?xml version="1.0" encoding="utf-8"?>', '<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">', '<ItemGroup Label="ProjectConfigurations">']
        for config in ["Debug", "Release"]:
            xml.append(f'<ProjectConfiguration Include="{config}|x64"><Configuration>{config}</Configuration><Platform>x64</Platform></ProjectConfiguration>')
        xml += ['</ItemGroup>', f'<PropertyGroup Label="Globals"><ProjectGuid>{GUIDS[name]}</ProjectGuid><ProjectName>{name}</ProjectName><Keyword>Win32Proj</Keyword><WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion></PropertyGroup>', '<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props"/>']
        for config in ["Debug", "Release"]:
            xml.append(f'<PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'{config}|x64\'" Label="Configuration"><ConfigurationType>{"StaticLibrary" if name == "nzcore" else "Application"}</ConfigurationType><UseDebugLibraries>{str(config == "Debug").lower()}</UseDebugLibraries><PlatformToolset>v143</PlatformToolset></PropertyGroup>')
        xml += ['<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props"/>', '<Import Project="ningzhou.props"/>', '<ItemGroup>']
        xml += [f'<{kind} Include="{escape(file)}"/>' for kind, file, _ in entries]
        xml += ['</ItemGroup>']
        if name != "nzcore":
            xml.append(f'<ItemGroup><ProjectReference Include="nzcore.vcxproj"><Project>{GUIDS["nzcore"]}</Project></ProjectReference></ItemGroup>')
        if name in {"ningzhou_web", "ningzhou_battle_lab"}:
            exclude = ' Exclude="..\\web\\battle-lab.html;..\\web\\battle-lab.mjs;..\\web\\battle-lab.css"' if name == "ningzhou_web" else ""
            asset_directory = "battle-lab-web" if name == "ningzhou_battle_lab" else "web"
            xml += [f'<ItemGroup><WebAssets Include="..\\web\\**\\*"{exclude}/></ItemGroup>', f'<Target Name="CopyWebAssets" AfterTargets="Build"><Copy SourceFiles="@(WebAssets)" DestinationFiles="@(WebAssets->\'$(OutDir){asset_directory}\\%(RecursiveDir)%(Filename)%(Extension)\')" SkipUnchangedFiles="true"/></Target>']
        xml += ['<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets"/>', '</Project>']
        write(name + ".vcxproj", "\n".join(xml) + "\n")
        groups = set()
        for _, _, group in entries:
            pieces = group.split("\\")
            groups.update("\\".join(pieces[:index]) for index in range(1, len(pieces) + 1))
        filters = ['<?xml version="1.0" encoding="utf-8"?>', '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup>']
        filters += [f'<Filter Include="{escape(group)}"><UniqueIdentifier>{{{str(uuid5(NAMESPACE_URL, group)).upper()}}}</UniqueIdentifier></Filter>' for group in sorted(groups)]
        filters += ['</ItemGroup><ItemGroup>'] + [f'<{kind} Include="{escape(file)}"><Filter>{escape(group)}</Filter></{kind}>' for kind, file, group in entries] + ['</ItemGroup></Project>']
        write(name + ".vcxproj.filters", "\n".join(filters) + "\n")
    solution = ['Microsoft Visual Studio Solution File, Format Version 12.00', '# Visual Studio Version 17', 'VisualStudioVersion = 17.0.31903.59', 'MinimumVisualStudioVersion = 10.0.40219.1']
    for name in PROJECTS:
        solution += [f'Project("{{BC8A1FFA-BEE3-4634-8014-F334798102B3}}") = "{name}", "{name}.vcxproj", "{GUIDS[name]}"', 'EndProject']
    solution += ['Global', '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution', '\t\tDebug|x64 = Debug|x64', '\t\tRelease|x64 = Release|x64', '\tEndGlobalSection', '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution']
    for name in PROJECTS:
        for config in ["Debug", "Release"]:
            solution += [f'\t\t{GUIDS[name]}.{config}|x64.ActiveCfg = {config}|x64', f'\t\t{GUIDS[name]}.{config}|x64.Build.0 = {config}|x64']
    solution += ['\tEndGlobalSection', 'EndGlobal']
    write("ningzhou.sln", "\n".join(solution) + "\n")
    print("Generated visual-studio/ningzhou.sln with relative paths and matching module filters.")


if __name__ == "__main__":
    generate()
