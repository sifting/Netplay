workspace "Netplay"
	configurations {"Debug", "Release"}
	location "build"
	targetdir "."
	debugdir "."
	filter "system:windows"
		links {"mingw32", "Ws2_32"}
	filter "configurations:Debug"
		defines {"DEBUG"}
		symbols "On"
	filter "configurations:Test"
		defines {"DEBUG", "TEST"}
		symbols "On"
	filter "configurations:Release"
		defines {"NDEBUG"}
		vectorextensions "Default"
		optimize "Speed"
	project "Netplay"
		language "C++"
		kind "ConsoleApp"
		files {
			"src/**.h",
			"src/**.cxx",
		}
		filter {}
