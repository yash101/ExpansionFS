#include "pch.h"
#include "FileProvider.h"

static const WCHAR* virtualization_path = nullptr;
static const WCHAR* source_path = nullptr;

static void help(int argc, const WCHAR** argv) {
	wprintf(L"ExpanderFS Help:\n");
	wprintf(L"-v    --virt-root     {path}      Selects the directory to be virtualized\n");
	wprintf(L"-s    --src-root      {path}      Selects the path at which files will be stored\n");
	wprintf(L"Base usage: %s --virt-root {virtualization root} --src-root {source root}", argv[0]);
}

int __cdecl wmain(int argc, const WCHAR** argv) {
	/*
	if (argc < 5) {
		help(argc, argv);
		return -1;
	}

	for (int i = 1; i < argc; i++) {
		if (!wcscmp(argv[i], L"-v") ||
			!wcscmp(argv[i], L"--virt-root")
		) {
			if (i++ == argc) {
				wprintf(L"Error: no argument provided after %s\n", argv[i - 1]);
				help(argc, argv);
				return -1;
			}

			virtualization_path = argv[i];
		}
		else if (!wcscmp(argv[i], L"-s") ||
			!wcscmp(argv[i], L"--src-root")
		) {
			if (i++ == argc) {
				wprintf(L"Error: no argument provided after %s\n", argv[i - 1]);
				help(argc, argv);
				return -1;
			}

			source_path = argv[i];
		}
		else {
			printf("Error: unrecognized command-line parameter %ws\n", argv[i]);
			help(argc, argv);
			return -1;
		}
	}

	if (virtualization_path == nullptr || source_path == nullptr) {
		wprintf(L"Error: I need a virtualization path, as well as a source path\n");
		help(argc, argv);
		return -1;
	}

	wprintf(
		L"Launching via parameters:\n\tvirtualization_path=[%s]\n\tsource_path=[%s]\n",
		virtualization_path,
		source_path
	);*/

	FileProvider provider;
//	provider.setVirtualizationPath(virtualization_path);
//	provider.setSourcePath(source_path);
	provider.setVirtualizationPath(L"C:\\Users\\yash\\Desktop\\VirtualShit\\FS");
	provider.setSourcePath(L"E:\\Apps");
	const WCHAR* output = provider.checkSanity();
	
	if (output != nullptr) {
		wprintf(output);
	}

	getchar();

	provider.startVirtualizing();

	return 0;
}
