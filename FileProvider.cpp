#include "pch.h"
#include "FileProvider.h"

#include <Windows.h>
#include <tchar.h>
#include <cassert>
#include <filesystem>
#include <cstdio>

// Helper functions used to convert types and sort
inline INT64 FT2I64(FILETIME ft) {
	return static_cast<INT64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}
inline INT64 MI32I32(DWORD LOW, DWORD HIGH) {
	return static_cast<INT64>(HIGH) << 32 | LOW;
}

// Initializes the object
FileProvider::FileProvider() :
	virtualizing(false)
{
}

// Deinitializes the object
FileProvider::~FileProvider()
{
	if (virtualizing) {
		PrjStopVirtualizing(instanceHandle);
		virtualizing = false;
	}

	for (auto it = open_files.begin(); it != open_files.end(); ++it) {
		fclose(it->second);
	}
}

// checkSanity makes sure the virtualization and source directories exist
const WCHAR* FileProvider::checkSanity() {
	// Check if any of the paths are undefined
	if (virtualization_path.empty() || source_path.empty()) {
		return L"Error: virtualization path and source path must be defined!";
	}

	// Remove the trailing //\ if it is in the paths
	for (size_t i = 0; i < virtualization_path.size(); i++) {
		if (virtualization_path[i] == L'/')
			virtualization_path[i] = L'\\';
	}

	for (size_t i = 0; i < source_path.size(); i++) {
		if (source_path[i] == L'/')
			source_path[i] = L'\\';
	}

	if (virtualization_path.back() == L'\\') {
		virtualization_path.pop_back();
	}

	if (source_path.back() == L'\\') {
		source_path.pop_back();
	}

	// Create directories for both roots if they don't exist
	
	if (!CreateDirectoryW(virtualization_path.c_str(), nullptr)) {
		DWORD err = GetLastError();
		if (err != ERROR_ALREADY_EXISTS) {
			return L"Error: could not create directory for the virtualization root";
		}
	}

	if (!CreateDirectoryW(source_path.c_str(), nullptr)) {
		DWORD err = GetLastError();
		if (err != ERROR_ALREADY_EXISTS) {
			return L"Error: could not create directory for the source path";
		}
	}

	return nullptr;
}

// This function sets up the virtualization environment and starts virtualizing
const WCHAR* FileProvider::startVirtualizing() {
	GUID instanceId;
	HRESULT hr = CoCreateGuid(&instanceId);
	if (FAILED(hr)) {
		return L"Error: failed to create instance ID!";
	}

	hr = PrjMarkDirectoryAsPlaceholder(
		virtualization_path.c_str(),
		nullptr,
		nullptr,
		&instanceId
	);

	if (FAILED(hr)) {
		return L"Error: unable to mark the virtualization root!";
	}

	PRJ_CALLBACKS callbacks;
	callbacks.StartDirectoryEnumerationCallback = startDirectoryEnumerationCB;
	callbacks.EndDirectoryEnumerationCallback = endDirectoryEnumerationCB;
	callbacks.GetDirectoryEnumerationCallback = getDirectoryEnumerationCB;
	callbacks.GetPlaceholderInfoCallback = getPlaceholderInfoCB;
	callbacks.GetFileDataCallback = getFileDataCB;
	callbacks.QueryFileNameCallback = queryFileNameCallback;
	callbacks.CancelCommandCallback = cancelCommandCB;

	PRJ_STARTVIRTUALIZING_OPTIONS options = PRJ_STARTVIRTUALIZING_OPTIONS();
	options.PoolThreadCount = 1;
	options.ConcurrentThreadCount = 1;

	hr = PrjStartVirtualizing(
		virtualization_path.c_str(),
		&callbacks,
		static_cast<const void*>(this),
		&options,
		&instanceHandle
	);

	if (FAILED(hr)) {
		return L"Error: failed to start the virtualization instance!";
	}

	virtualizing = true;
	return 0;
}

// The SourceFileSystemWorker performs the I/O on the target disk
void FileProvider::SourceFileSystemWorker(FileProvider* provider)
{
}

/*
	callbackData holds information about the operation
		callbackData.FilePathName is the directory to be enumerated
		callbackData.VersionInfo provides version information for the directory to be enumerated
	enumerationId is an identifier for this enumeration session

	Returns:
		S_OK if the provider successfully completed the operation
		HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) if the directory doesn't exist in the store
		HRESULT_FROM_WIN32(ERROR_IO_PENDING) if the provider wishes to complete the operation later
*/
HRESULT FileProvider::startDirectoryEnumerationCB(
	const PRJ_CALLBACK_DATA* callbackData,
	const GUID* enumerationId
) {
	// Get a pointer to the virtualization instance object
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);

	// VirtPath + callbackData->FilePathName = virtualized path
	// SourcePath + callbackData->FilePathName = source path (most likely)

	// Create a new session object
	FileProvider::EnumerationSession session;
	session.virt_path = provider->virtualization_path + L"\\" + callbackData->FilePathName;
	session.src_path = provider->source_path + L"\\" + callbackData->FilePathName;

	// Check to make sure the file exists
	DWORD ftype = GetFileAttributesW(session.src_path.c_str());
	if (ftype == INVALID_FILE_ATTRIBUTES && !(ftype & FILE_ATTRIBUTE_DIRECTORY))
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

	// Add the session object to the enumerations hash map
	provider->enumerations[*enumerationId] = session;

	// Enumerate the directory and fill out the data structures
	session.enumerate();

	// Success!
	return S_OK;
}

/*
	callbackData holds information about the operation
	enumerationId holds an ID for the enumeration

	Returns:
		S_OK if the provider successfully completes the operation
		HRESULT_FROM_WIN32(ERROR_IO_PENDING) if the provider wishes to complete the operation later
*/
HRESULT FileProvider::endDirectoryEnumerationCB(
	const PRJ_CALLBACK_DATA* callbackData,
	const GUID * enumerationId
) {
	// Get a pointer to the virtualization instance object
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);

	// Erase the enumeration object
	provider->enumerations.erase(*enumerationId);

	// Success
	return S_OK;
}

/*
	callbackData holds information about the operation
		callbackData.FilePathName is the directory to be enumerated
		callbackData.VersionInfo provides version information for the directory being enumerated
		callbackData.Flags controls what can be returned in the enumeration
			PRJ_CB_DATA_FLAG_ENUM_RETURN_SINGLE_ENTRY = request one entry from the enumeration
			PRJ_CB_DATA_FLAG_ENUM_RESTART_SCAN = start at the beginning of the directory
	enumerationId holds an ID for the enumeration
	searchExpression holds a search expression
		- Use PrjDoesNameContainWildCards() and PrjFileNameMatch() to assist
	dirEntryBufferHandle is a handle to a structure which receives the results of the enumeration
		- PrjFillDirEntryBuffer() fills out this structure

	Returns:
		S_OK if at least one entry was added to the dirEntryBufferHandle, or if no entries match searchExpression
		HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) if this error was received when adding the first file via
			PrjFillDirEntryBuffer()
		HRESULT_FROM_WIN32(ERROR_IO_PENDING) if the provider wishes to complete the operation later
*/
HRESULT FileProvider::getDirectoryEnumerationCB(
	const PRJ_CALLBACK_DATA* callbackData,
	const GUID * enumerationId,
	PCWSTR searchExpression,
	PRJ_DIR_ENTRY_BUFFER_HANDLE dirEntryBufferHandle
) {
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);

	if (provider->enumerations.find(*enumerationId) == provider->enumerations.end()) {
		return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
	}
	EnumerationSession& session = provider->enumerations[*enumerationId];
	if (!session.search_expression_captured ||
		(callbackData->Flags & PRJ_CB_DATA_FLAG_ENUM_RESTART_SCAN)
	) {
		if (searchExpression != NULL) {
			if (wcsncpy_s(
				session.search_expression,
				session.search_expression_max_length,
				searchExpression,
				wcslen(searchExpression))
			) {
				// failed to copy the search expression; provider should try
				// reallocating session->SearchExpression
			}
		} else {
			if (wcsncpy_s(
				session.search_expression,
				session.search_expression_max_length,
				L"*",
				1
			)) {
				// Failed to copy the search expression; provider should try reallocating
				// session->SearchExpression
			}
		}

		session.search_expression_captured = TRUE;
	}

	EnumerationSession::EnumerationEntry* enumHead = NULL;

	// Start from the beginning if we aren't continuing an existing session or if the caller
	// is requesting a restart
	if (((session.enum_last == NULL) &&
		!session.enum_completed) ||
		(callbackData->Flags & PRJ_CB_DATA_FLAG_ENUM_RESTART_SCAN)
	) {
		// Ensure that if the caller is requesting a restart, we reset our current position
		session.enum_last = NULL;

		// In case we are restarting
		session.enum_completed = FALSE;

		// Start from the beginning of the list from the store
		enumHead = session.enum_head;
	} else {
		
		// Resuming an existing enumeration session. Note that session->LastEnumEntry
		// may be NULL if we got ll the entries the last time this callback was invoked

		// Returning S_OK without adding new entries to the dirEntryBufferHandle buffer
		// has returend everything it can
		enumHead = session.enum_head;
	}

	if (enumHead == NULL) {
		// No items to return; we have returned everything we can
		session.enum_completed = TRUE;
	} else {
		EnumerationSession::EnumerationEntry* thisEntry = enumHead;
		while (thisEntry != NULL) {

			// Insert the entry into the return buffer if it matches the search expression
			// captured for this enumeration session
			if (PrjFileNameMatch(
				thisEntry->name.c_str(),
				session.search_expression
			)) {
				PRJ_FILE_BASIC_INFO fileBasicInfo = thisEntry->fileInfo;

				// Format the entry for return to ProjFS
				if (PrjFillDirEntryBuffer(
					thisEntry->name.c_str(),
					&fileBasicInfo,
					dirEntryBufferHandle
				) != S_OK) {
					session.enum_last = thisEntry;
					return S_OK;
				}
			}

			// Jump to the next entry
			thisEntry = thisEntry->next;
		}

		// Reached the end of the list of entries; returned everything we can
		session.enum_completed = TRUE;
	}

	return S_OK;
}

/*
	callbackData holds information about the operation
		callbackData.FilePathName identifies the path to the file or directory in the provider's store for which
			information is requested
			- Use PrjFileNameMatch() to determine to compare names in the storage
			- destinationFileName argument of PrjWritePlaceholderInfo()
		callbackData.versionInfo provides version information for the parent directory of the requested item

	Returns:
		S_OK if the file exists and the information was provided to ProjFS
		HRESULT_FROM_WIN32(ERROR_IO_PENDING) if the provider wishes to complete the operation later
		HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) if the file does not exist in the storage
*/
HRESULT FileProvider::getPlaceholderInfoCB(
	const PRJ_CALLBACK_DATA* callbackData
) {
	// Pointer to the virtualization instance object
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);

	// Form the backing path
	std::wstring backing_path = provider->source_path + L"\\" + callbackData->FilePathName;

	GET_FILEEX_INFO_LEVELS FileInfosLevel;
	WIN32_FILE_ATTRIBUTE_DATA attributes;
	// Get the attributes for the file in the backing path
	if (!GetFileAttributesExW(backing_path.c_str(), GetFileExInfoStandard, static_cast<LPVOID>(&attributes))) {
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	}

	PRJ_PLACEHOLDER_INFO placeholderInfo = {};
	placeholderInfo.FileBasicInfo.IsDirectory =
		attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? TRUE : FALSE;
	placeholderInfo.FileBasicInfo.FileSize = MI32I32(attributes.nFileSizeLow, attributes.nFileSizeHigh);
	placeholderInfo.FileBasicInfo.CreationTime.QuadPart =
		MI32I32(attributes.ftCreationTime.dwLowDateTime, attributes.ftCreationTime.dwHighDateTime);
	placeholderInfo.FileBasicInfo.LastAccessTime.QuadPart =
		MI32I32(attributes.ftLastAccessTime.dwLowDateTime, attributes.ftLastAccessTime.dwHighDateTime);
	placeholderInfo.FileBasicInfo.LastWriteTime.QuadPart =
		MI32I32(attributes.ftLastWriteTime.dwLowDateTime, attributes.ftLastWriteTime.dwHighDateTime);
	placeholderInfo.FileBasicInfo.ChangeTime.QuadPart =
		MI32I32(attributes.ftLastWriteTime.dwLowDateTime, attributes.ftLastWriteTime.dwHighDateTime);
	placeholderInfo.FileBasicInfo.FileAttributes = attributes.dwFileAttributes;

	return PrjWritePlaceholderInfo(
		callbackData->NamespaceVirtualizationContext,
		backing_path.c_str(),
		&placeholderInfo,
		sizeof(placeholderInfo));
}


/*
	callbackData holds information about the operation
		callbackData.FilePathName identifies the path to the file/directory  in the store, for which data should be returned
			- Reflects the name the file had when its placeholder was created; if file was renamed, FilePathName identifies the
				original name, not the current name
		callbackData.DataStreamId is a unique value associated with the file stream
			- Provider must pass this data to PrjWriteFileData() when providing file data in this callback
		callbackData.VersionInfo provides PRJ_PLACEHOLDER_VERSION_INFO supplied by the provider when the placeholder for the file
			was created
				- Helps provider determine which version of the file contents to return
	byteOffset holds the offset of the requested data, in bytes, from the beginning of the file
		- Data must start from here
	length hold the number of bytes requested
		- Must return at least `length` bytes of data, byte = octet

	Returns:
		S_OK if the provider successfully returned all the requested data
		HRESULT_FROM_WIN32(ERROR_IO_PENDING) if the provider wishes to complete the request later

	The provider must issue 1+ calls to PrjWriteFileData to give ProjFS the requested contents of the file's primary data stream
*/
#define BlockAlignTruncate(P,V) ((P) & (0-((UINT64)(V))))
HRESULT FileProvider::getFileDataCB(
	const PRJ_CALLBACK_DATA* callbackData,
	UINT64 byteOffset,
	UINT32 length
) {
	HRESULT hr;
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);

	std::wstring path = callbackData->FilePathName;

/*	if (provider->open_files.find(path) == provider->open_files.end()) {
		LPOFSTRUCT info;
		provider->open_files[path] = CreateFileW(
			path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
			NULL
		);

		if (provider->open_files[path] == INVALID_HANDLE_VALUE) {
			DWORD err = GetLastError();
			return err;
		}
	}

	HANDLE& h = provider->open_files[path]; */

	FILE* fil = _wfopen(path.c_str(), "r");

	UINT64 writeStartOffset;
	UINT32 writeLength;
	if (length <= 1024 * 1024) {
		writeStartOffset = byteOffset;
		writeLength = length;
	} else {
		PRJ_VIRTUALIZATION_INSTANCE_INFO instanceInfo;
		UINT32 infoSize = sizeof(instanceInfo);
		hr = PrjGetVirtualizationInstanceInfo(
			callbackData->NamespaceVirtualizationContext,
			&instanceInfo
		);

		if (FAILED(hr)) {
			return hr;
		}

		writeStartOffset = byteOffset;
		UINT64 writeEndOffset = BlockAlignTruncate(
			writeStartOffset + 1024 * 1024,
			instanceInfo.WriteAlignment
		);
		assert(writeEndOffset > 0);
		assert(writeEndOffset > writeStartOffset);

		writeLength = writeEndOffset - writeStartOffset;
	}

	void* writeBuffer = NULL;
	writeBuffer = PrjAllocateAlignedBuffer(
		callbackData->NamespaceVirtualizationContext,
		writeLength
	);

	if (writeBuffer == NULL) {
		return E_OUTOFMEMORY;
	}

	do {
		// hr = GetDataFromStore(callbackData->FilePathName,
		//	writeStartOffset, writeLength, writeBuffer);
#pragma warning("Error, not implemented!");

		if (FAILED(hr)) {
			PrjFreeAlignedBuffer(writeBuffer);
			return hr;
		}

		length -= writeLength;
		if (length < writeLength) {
			writeLength = length;
		}
	} while (writeLength > 0);

	PrjFreeAlignedBuffer(writeBuffer);
	return hr;
}

/*
	callbackInfo holds information about the operation

	Returns:
		S_OK if the queried path exists in the store
		HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) if the queried path does not exist
		HRESULT_FROM_WIN32(ERROR_IO_PENDING) if the provider would like to complete the operation later
*/
HRESULT FileProvider::queryFileNameCallback(
	const PRJ_CALLBACK_DATA* callbackData
) {
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);
	return E_NOTIMPL;
}

/*
	callbackData holds information about the operation
		callbackData.FilePathName identifies the path for the file or directory to which the
			notification pertains
	isDirectory is TRUE if FilePathName is a directory
	notification is a PRJ_NOTIFICATION specifying the notification
	destinationFileName - if notification is PRJ_NOTIFICATION_PRE_RENAME or
		PRJ_NOTIFICATION_PRE_SET_HARDLINK, this points to string specifying the path relative to
		virtualization root of the target of the rename or set-hardlink operation
	operationParameters specifies extra parameters for certain values of notifications:
		PRJ_NOTIFICATION_FILE_OPENED / PRJ_NOTIFICATION_NEW_FILE_CREATED / PRJ_NOTIFICATION_FILE_OVERWRITTEN:
			Fields of operationParameters.PostCreate are specified:
				NotificationMask: upon return from PRJ_NOTIFICATION_CB callback, the provider
					may specify a new set of notifications it wishes to receive for the file
		PRJ_NOTIFICATION_FILE_RENAMED:
			Fields of operatingParameters.FileRenamed are specified:
				operationParameters.FileRenamed.NoficiationMask:
					Upon return from the callback, provider may specify a new set of noficiations
						it wishes to receive for the file here
		PRJ_NOTIFICATION_FILE_HANDLE_CLOSED_FILE_DELETED:
			operationParameters.NotificationMask is specified.
			If the provider registered for PRJ_NOTIFY_FILE_HANDLE_CLOSED_FILE_MODIFIED as well as
				PRJ_NOTIFY_FILE_HANDLE_CLOSED_FILE_DELETED, this field is set to TRUE if the file was modified
				before it was deleted
	
	Returns:
		S_OK if the provider successfully processed the notification
		HRESULT_FROM_WIN32(ERROR_IO_PENDING) if the provider wishes to complete the operation later
*/
HRESULT FileProvider::notificationCB(
	const PRJ_CALLBACK_DATA* callbackData,
	BOOLEAN isDirectory,
	PRJ_NOTIFICATION notificationType,
	PCWSTR destinationFileName,
	PRJ_NOTIFICATION_PARAMETERS* notificationParameters
) {
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);
	return E_NOTIMPL;
}

/*
	callbackData contains information about the operation
		callbackData.CommandId identifies the operation to be cancelled

	Returns:
		void
*/
void FileProvider::cancelCommandCB(
	const PRJ_CALLBACK_DATA* callbackData
) {
	FileProvider* provider = reinterpret_cast<FileProvider*>(callbackData->InstanceContext);
}

/*
	left contains the left operand GUID
	right contains the right operand GUID

	Returns:
		bool - true if left < right; false if left >= right
*/
bool FileProvider::GUIDComparer::operator()(const GUID & left, const GUID & right) const
{
	// Compare the GUIDs by their data
	return memcmp(
		static_cast<const void*>(&left),
		static_cast<const void*>(&right),
		sizeof(left)
	) < 0;
}

// Holds information temporarily so it can be added to a vector
static struct TmpTupleEnumerate {
	std::wstring name;
	PRJ_FILE_BASIC_INFO fInfo;
};

static bool lessThan(TmpTupleEnumerate a, TmpTupleEnumerate b) {
	return PrjFileNameCompare(a.name.c_str(), b.name.c_str()) < 0;
}

HRESULT FileProvider::EnumerationSession::enumerate() {
	// Get a list of the files and directories in the directory
	WIN32_FIND_DATA fileData;
	HANDLE hFind;

	// Holds all the data temporarily
	std::vector<TmpTupleEnumerate> files;

	// Find the first file/directory
	hFind = FindFirstFileW(src_path.c_str(), &fileData);

	// Check to see if no files or directories exist
	if (hFind == INVALID_HANDLE_VALUE &&
		GetLastError() == ERROR_FILE_NOT_FOUND) {
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	}
	
	// Add the first file
	{
		TmpTupleEnumerate en;
		en.name = fileData.cFileName;
		en.fInfo.IsDirectory =
			(fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
		en.fInfo.FileSize =
			(fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
			0 : MI32I32(fileData.nFileSizeLow, fileData.nFileSizeHigh);
		en.fInfo.LastAccessTime.QuadPart = FT2I64(fileData.ftLastAccessTime);
		en.fInfo.LastWriteTime.QuadPart = FT2I64(fileData.ftLastWriteTime);
		en.fInfo.CreationTime.QuadPart = FT2I64(fileData.ftCreationTime);
		en.fInfo.ChangeTime.QuadPart = FT2I64(fileData.ftLastWriteTime);

		files.push_back(en);
	}

	// Add the files
	while (FindNextFileW(hFind, &fileData)) {
		TmpTupleEnumerate en;
		en.name = fileData.cFileName;
		en.fInfo.IsDirectory =
			(fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
		en.fInfo.FileSize =
			(fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
			0 : MI32I32(fileData.nFileSizeLow, fileData.nFileSizeHigh);
		en.fInfo.LastAccessTime.QuadPart = FT2I64(fileData.ftLastAccessTime);
		en.fInfo.LastWriteTime.QuadPart = FT2I64(fileData.ftLastWriteTime);
		en.fInfo.CreationTime.QuadPart = FT2I64(fileData.ftCreationTime);
		en.fInfo.ChangeTime.QuadPart = FT2I64(fileData.ftLastWriteTime);

		files.push_back(en);
	}

	FindClose(hFind);

	// Sort the files
	std::sort(files.begin(), files.end(), lessThan);

	// Convert to the linked list
	for (auto it = files.begin(); it != files.end(); ++it) {
		EnumerationEntry* entry = new EnumerationEntry;
		entry->fileInfo = it->fInfo;
		entry->name = it->name;
		entry->next = NULL;

		if (enum_head == NULL) {
			enum_head = entry;
			enum_back = entry;
		} else {
			enum_back->next = entry;
			enum_back = entry;
		}
	}

	return S_OK;
}