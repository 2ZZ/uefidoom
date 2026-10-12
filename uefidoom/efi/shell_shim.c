// Minimal stand-in for the UEFI Shell, so doom.efi can be launched directly
// by firmware or a boot manager (e.g. rEFInd) instead of only from the Shell.
//
// EDK II LibC gets argv and all file I/O through the Shell protocols. When
// they are absent we install our own EFI_SHELL_PARAMETERS_PROTOCOL and a
// partial EFI_SHELL_PROTOCOL whose file functions operate on the volume
// doom.efi was loaded from, with relative paths resolved against its folder.

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FileHandleLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/ShellLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/EfiShellInterface.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>
#include <Protocol/SimpleFileSystem.h>

#define MAX_ARGS 64

EFI_STATUS EFIAPI ShellCEntryLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);

static EFI_SHELL_PROTOCOL gShimShell;
static EFI_SHELL_PARAMETERS_PROTOCOL gShimParams;
static EFI_FILE_PROTOCOL* gShimRoot = NULL;
static EFI_FILE_PROTOCOL* gShimDir = NULL;
static CHAR16* gShimCwd = NULL;
static CHAR16* gShimCmdLine = NULL;
static CHAR16* gShimArgv[MAX_ARGS];

////////////////////////////////////////////////////////////////////

static UINTN EFIAPI ShimUnsupported(void)
{
	return EFI_UNSUPPORTED;
}

static UINTN EFIAPI ShimNull(void)
{
	return 0;
}

// Open a path relative to doom.efi's folder, or to the volume root if it
// starts with a backslash. Any "fsN:" style prefix is ignored.
static EFI_STATUS ShimOpen(CONST CHAR16* FileName, EFI_FILE_PROTOCOL** File, UINT64 OpenMode, UINT64 Attributes)
{
	EFI_FILE_PROTOCOL* base = gShimDir;
	CHAR16* path;
	CHAR16* p;
	CHAR16* colon;
	EFI_STATUS status;

	if (FileName == NULL || File == NULL)
	{
		return EFI_INVALID_PARAMETER;
	}

	path = AllocateCopyPool(StrSize(FileName), FileName);
	if (path == NULL)
	{
		return EFI_OUT_OF_RESOURCES;
	}

	for (p = path; *p != 0; ++p)
	{
		if (*p == L'/')
		{
			*p = L'\\';
		}
	}

	p = path;
	colon = StrStr(p, L":");
	if (colon != NULL)
	{
		p = colon + 1;
		base = gShimRoot;
	}
	if (*p == L'\\')
	{
		base = gShimRoot;
	}
	while (p[0] == L'.' && p[1] == L'\\')
	{
		p += 2;
	}
	if (*p == 0)
	{
		p = L".";
	}

	status = base->Open(base, File, p, OpenMode, Attributes);
	FreePool(path);
	return status;
}

static EFI_STATUS EFIAPI ShimOpenFileByName(CONST CHAR16* FileName, SHELL_FILE_HANDLE* FileHandle, UINT64 OpenMode)
{
	return ShimOpen(FileName, (EFI_FILE_PROTOCOL**)FileHandle, OpenMode, 0);
}

static EFI_STATUS EFIAPI ShimCreateFile(CONST CHAR16* FileName, UINT64 FileAttribs, SHELL_FILE_HANDLE* FileHandle)
{
	return ShimOpen(FileName, (EFI_FILE_PROTOCOL**)FileHandle,
		EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, FileAttribs);
}

static CONST CHAR16* EFIAPI ShimGetCurDir(CONST CHAR16* FileSystemMapping)
{
	return gShimCwd;
}

////////////////////////////////////////////////////////////////////

// Build "\EFI\doom" style folder path from the image's file path nodes
static CHAR16* ShimImageDir(EFI_DEVICE_PATH_PROTOCOL* node)
{
	CHAR16* dir = AllocateZeroPool(sizeof(CHAR16) * 512);
	UINTN len = 0;
	UINTN i;

	if (dir == NULL)
	{
		return NULL;
	}

	for (; node != NULL && !IsDevicePathEnd(node); node = NextDevicePathNode(node))
	{
		if (DevicePathType(node) == MEDIA_DEVICE_PATH && DevicePathSubType(node) == MEDIA_FILEPATH_DP)
		{
			CONST CHAR16* name = ((FILEPATH_DEVICE_PATH*)node)->PathName;
			if (len > 0 && dir[len - 1] != L'\\' && name[0] != L'\\')
			{
				dir[len++] = L'\\';
			}
			for (i = 0; name[i] != 0 && len < 510; ++i)
			{
				dir[len++] = name[i];
			}
		}
	}

	// Drop the file name, keep the folder
	while (len > 0 && dir[len - 1] != L'\\')
	{
		--len;
	}
	if (len > 1)
	{
		--len;
	}
	if (len == 0)
	{
		dir[len++] = L'\\';
	}
	dir[len] = 0;
	return dir;
}

// Split LoadOptions into argv. Boot managers usually pass the loader path as
// the first word; if they don't, supply a program name ourselves.
static void ShimParseArgs(EFI_LOADED_IMAGE_PROTOCOL* image)
{
	UINTN count = image->LoadOptionsSize / sizeof(CHAR16);
	UINTN argc = 0;
	CHAR16* p;

	gShimParams.Argv = gShimArgv;
	gShimParams.Argc = 1;
	gShimArgv[0] = L"doom.efi";

	if (image->LoadOptions == NULL || count == 0)
	{
		return;
	}

	gShimCmdLine = AllocateZeroPool((count + 1) * sizeof(CHAR16));
	if (gShimCmdLine == NULL)
	{
		return;
	}
	CopyMem(gShimCmdLine, image->LoadOptions, count * sizeof(CHAR16));

	for (p = gShimCmdLine; *p != 0; ++p)
	{
		// Not a text command line (e.g. a binary boot option payload)
		if (*p < 0x20 && *p != L'\t')
		{
			return;
		}
	}

	p = gShimCmdLine;
	while (*p != 0 && argc < MAX_ARGS)
	{
		CHAR16 end = L' ';
		while (*p == L' ' || *p == L'\t')
		{
			++p;
		}
		if (*p == 0)
		{
			break;
		}
		if (*p == L'"')
		{
			end = L'"';
			++p;
		}
		gShimArgv[argc++] = p;
		while (*p != 0 && *p != end && !(end == L' ' && *p == L'\t'))
		{
			++p;
		}
		if (*p != 0)
		{
			*p++ = 0;
		}
	}

	if (argc == 0)
	{
		return;
	}

	{
		UINTN len = StrLen(gShimArgv[0]);
		CONST CHAR16* ext = gShimArgv[0] + len - 4;
		BOOLEAN isProgram = len >= 4 && ext[0] == L'.' &&
			(ext[1] | 0x20) == L'e' && (ext[2] | 0x20) == L'f' && (ext[3] | 0x20) == L'i';
		if (!isProgram)
		{
			if (argc == MAX_ARGS)
			{
				--argc;
			}
			CopyMem(&gShimArgv[1], &gShimArgv[0], argc * sizeof(CHAR16*));
			gShimArgv[0] = L"doom.efi";
			++argc;
		}
	}
	gShimParams.Argc = argc;
}

static EFI_STATUS ShimInit(EFI_HANDLE ImageHandle)
{
	EFI_LOADED_IMAGE_PROTOCOL* image;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;
	EFI_STATUS status;

	status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&image);
	if (EFI_ERROR(status))
	{
		return status;
	}

	status = gBS->HandleProtocol(image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&fs);
	if (EFI_ERROR(status))
	{
		return status;
	}

	status = fs->OpenVolume(fs, &gShimRoot);
	if (EFI_ERROR(status))
	{
		return status;
	}

	gShimCwd = ShimImageDir(image->FilePath);
	if (gShimCwd == NULL)
	{
		return EFI_OUT_OF_RESOURCES;
	}

	status = gShimRoot->Open(gShimRoot, &gShimDir, gShimCwd, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status))
	{
		gShimDir = gShimRoot;
	}

	ShimParseArgs(image);

	// Anything not implemented reports unsupported, or NULL/FALSE for
	// functions returning pointers or booleans.
	gShimShell.Execute                   = (EFI_SHELL_EXECUTE)ShimUnsupported;
	gShimShell.GetEnv                    = (EFI_SHELL_GET_ENV)ShimNull;
	gShimShell.SetEnv                    = (EFI_SHELL_SET_ENV)ShimUnsupported;
	gShimShell.GetAlias                  = (EFI_SHELL_GET_ALIAS)ShimNull;
	gShimShell.SetAlias                  = (EFI_SHELL_SET_ALIAS)ShimUnsupported;
	gShimShell.GetHelpText               = (EFI_SHELL_GET_HELP_TEXT)ShimUnsupported;
	gShimShell.GetDevicePathFromMap      = (EFI_SHELL_GET_DEVICE_PATH_FROM_MAP)ShimNull;
	gShimShell.GetMapFromDevicePath      = (EFI_SHELL_GET_MAP_FROM_DEVICE_PATH)ShimNull;
	gShimShell.GetDevicePathFromFilePath = (EFI_SHELL_GET_DEVICE_PATH_FROM_FILE_PATH)ShimNull;
	gShimShell.GetFilePathFromDevicePath = (EFI_SHELL_GET_FILE_PATH_FROM_DEVICE_PATH)ShimNull;
	gShimShell.SetMap                    = (EFI_SHELL_SET_MAP)ShimUnsupported;
	gShimShell.GetCurDir                 = ShimGetCurDir;
	gShimShell.SetCurDir                 = (EFI_SHELL_SET_CUR_DIR)ShimUnsupported;
	gShimShell.OpenFileList              = (EFI_SHELL_OPEN_FILE_LIST)ShimUnsupported;
	gShimShell.FreeFileList              = (EFI_SHELL_FREE_FILE_LIST)ShimUnsupported;
	gShimShell.RemoveDupInFileList       = (EFI_SHELL_REMOVE_DUP_IN_FILE_LIST)ShimUnsupported;
	gShimShell.BatchIsActive             = (EFI_SHELL_BATCH_IS_ACTIVE)ShimNull;
	gShimShell.IsRootShell               = (EFI_SHELL_IS_ROOT_SHELL)ShimNull;
	gShimShell.EnablePageBreak           = (EFI_SHELL_ENABLE_PAGE_BREAK)ShimNull;
	gShimShell.DisablePageBreak          = (EFI_SHELL_DISABLE_PAGE_BREAK)ShimNull;
	gShimShell.GetPageBreak              = (EFI_SHELL_GET_PAGE_BREAK)ShimNull;
	gShimShell.GetDeviceName             = (EFI_SHELL_GET_DEVICE_NAME)ShimUnsupported;
	gShimShell.GetFileInfo               = (EFI_SHELL_GET_FILE_INFO)FileHandleGetInfo;
	gShimShell.SetFileInfo               = (EFI_SHELL_SET_FILE_INFO)FileHandleSetInfo;
	gShimShell.OpenFileByName            = ShimOpenFileByName;
	gShimShell.CloseFile                 = (EFI_SHELL_CLOSE_FILE)FileHandleClose;
	gShimShell.CreateFile                = ShimCreateFile;
	gShimShell.ReadFile                  = (EFI_SHELL_READ_FILE)FileHandleRead;
	gShimShell.WriteFile                 = (EFI_SHELL_WRITE_FILE)FileHandleWrite;
	gShimShell.DeleteFile                = (EFI_SHELL_DELETE_FILE)FileHandleDelete;
	gShimShell.DeleteFileByName          = (EFI_SHELL_DELETE_FILE_BY_NAME)ShimUnsupported;
	gShimShell.GetFilePosition           = (EFI_SHELL_GET_FILE_POSITION)FileHandleGetPosition;
	gShimShell.SetFilePosition           = (EFI_SHELL_SET_FILE_POSITION)FileHandleSetPosition;
	gShimShell.FlushFile                 = (EFI_SHELL_FLUSH_FILE)FileHandleFlush;
	gShimShell.FindFiles                 = (EFI_SHELL_FIND_FILES)ShimUnsupported;
	gShimShell.FindFilesInDir            = (EFI_SHELL_FIND_FILES_IN_DIR)ShimUnsupported;
	gShimShell.GetFileSize               = (EFI_SHELL_GET_FILE_SIZE)FileHandleGetSize;
	gShimShell.OpenRoot                  = (EFI_SHELL_OPEN_ROOT)ShimUnsupported;
	gShimShell.OpenRootByHandle          = (EFI_SHELL_OPEN_ROOT_BY_HANDLE)ShimUnsupported;
	gShimShell.ExecutionBreak            = NULL;
	gShimShell.MajorVersion              = 2;
	gShimShell.MinorVersion              = 0;
	gShimShell.RegisterGuidName          = (EFI_SHELL_REGISTER_GUID_NAME)ShimUnsupported;
	gShimShell.GetGuidName               = (EFI_SHELL_GET_GUID_NAME)ShimUnsupported;
	gShimShell.GetGuidFromName           = (EFI_SHELL_GET_GUID_FROM_NAME)ShimUnsupported;
	gShimShell.GetEnvEx                  = (EFI_SHELL_GET_ENV_EX)ShimNull;

	return gBS->InstallMultipleProtocolInterfaces(&ImageHandle,
		&gEfiShellParametersProtocolGuid, &gShimParams,
		&gEfiShellProtocolGuid, &gShimShell,
		NULL);
}

static void ShimShutdown(EFI_HANDLE ImageHandle)
{
	gBS->UninstallMultipleProtocolInterfaces(ImageHandle,
		&gEfiShellParametersProtocolGuid, &gShimParams,
		&gEfiShellProtocolGuid, &gShimShell,
		NULL);

	if (gShimDir != NULL && gShimDir != gShimRoot)
	{
		gShimDir->Close(gShimDir);
	}
	if (gShimRoot != NULL)
	{
		gShimRoot->Close(gShimRoot);
	}
	if (gShimCwd != NULL)
	{
		FreePool(gShimCwd);
	}
	if (gShimCmdLine != NULL)
	{
		FreePool(gShimCmdLine);
	}
}

////////////////////////////////////////////////////////////////////

// Module entry point (see doom.inf)
EFI_STATUS EFIAPI DoomEfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
	VOID* protocol;
	EFI_STATUS status;

	if (!EFI_ERROR(gBS->HandleProtocol(ImageHandle, &gEfiShellParametersProtocolGuid, &protocol)) ||
		!EFI_ERROR(gBS->HandleProtocol(ImageHandle, &gEfiShellInterfaceGuid, &protocol)))
	{
		// Launched from a real UEFI Shell
		ShellInitialize();
		return ShellCEntryLib(ImageHandle, SystemTable);
	}

	status = ShimInit(ImageHandle);
	if (EFI_ERROR(status))
	{
		Print(L"doom: failed to set up file access without a UEFI Shell: %r\n", status);
		return status;
	}

	ShellInitialize();
	status = ShellCEntryLib(ImageHandle, SystemTable);
	ShimShutdown(ImageHandle);

	// A boot manager redraws its menu straight away, so keep any error
	// message on screen until a key is pressed.
	if (status != EFI_SUCCESS)
	{
		EFI_INPUT_KEY key;
		UINTN index;

		Print(L"\nPress any key to exit.\n");
		gST->ConIn->Reset(gST->ConIn, FALSE);
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &index);
		gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
	}

	return status;
}
