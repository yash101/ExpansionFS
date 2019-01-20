#pragma once

#include "pch.h"
#include <map>
#include <mutex>
#include <string>
#include <cstdio>
#include <unordered_map>

class FileMetadata {
};

class FileProvider
{
protected:

	class GUIDComparer {
	public:
		bool operator() (const GUID& left, const GUID& right) const;
	};

	// Classes that hold information used during runtime
	class EnumerationSession {
	public:

		class EnumerationEntry {
		public:
			EnumerationEntry* next;
			std::wstring name;
			PRJ_FILE_BASIC_INFO fileInfo;

			EnumerationEntry() :
				next(NULL),
				name(NULL),
				fileInfo({})
			{}
			~EnumerationEntry() {
				EnumerationEntry* e = next;
				while (next != NULL) {
					EnumerationEntry* tmp = e->next;
					delete next;
					e = tmp;
				}
			}
		};
	
		GUID enumeration_id;

		std::wstring virt_path;
		std::wstring src_path;
		PWSTR search_expression;
		USHORT search_expression_max_length;

		BOOLEAN search_expression_captured;
		BOOLEAN enum_completed;

		EnumerationEntry* enum_head;
		EnumerationEntry* enum_back;
		EnumerationEntry* enum_last;

		HRESULT enumerate();

		EnumerationSession() :
			enumeration_id(),
			search_expression(NULL),
			search_expression_max_length(0),
			enum_head(NULL),
			enum_last(NULL),
			enum_back(NULL)
		{}

		~EnumerationSession() {
			if (enum_head)
				delete enum_head;
		}
	};

	class SourceFileSystemJob {
	public:
		SourceFileSystemJob* prev;
		SourceFileSystemJob* next;
		int type;
		std::string file_from;
		std::string file_to;
		UINT64 offset;
		UINT32 length;

		static const int TYPE_READ = 0;
		static const int TYPE_WRITE = 1;
		static const int TYPE_DIRECTORY_ENUM = 2;

		SourceFileSystemJob();
		~SourceFileSystemJob();
	};

	// Shared variables
	std::wstring virtualization_path;
	std::wstring source_path;
	PRJ_NAMESPACE_VIRTUALIZATION_CONTEXT instanceHandle;
	bool virtualizing;
	std::map<GUID, EnumerationSession, GUIDComparer> enumerations;
	SourceFileSystemJob* sourceJobsHead;
	SourceFileSystemJob* sourceJobsEnd;
	std::mutex sourceJobsMutex;
	std::unordered_map<std::wstring, HANDLE> open_files;

	// Functions

	// SourceFileSystemWorker runs in a thread and performs I/O quickly and efficiently
	static void SourceFileSystemWorker(FileProvider* provider);

	// Starts the enumeration of a directory
	static HRESULT startDirectoryEnumerationCB(
		_In_ const PRJ_CALLBACK_DATA* callbackData,
		_In_ const GUID* enumerationId
	);

	// Ends the enumeration of a directory
	static HRESULT endDirectoryEnumerationCB(
		_In_ const PRJ_CALLBACK_DATA* callbackData,
		_In_ const GUID* enumerationId
	);

	static HRESULT getDirectoryEnumerationCB(
		_In_ const PRJ_CALLBACK_DATA* callbackData,
		_In_ const GUID* enumerationId,
		_In_opt_ PCWSTR searchExpression,
		_In_ PRJ_DIR_ENTRY_BUFFER_HANDLE dirEntryBufferHandle
	);

	static HRESULT getPlaceholderInfoCB(
		_In_ const PRJ_CALLBACK_DATA* callbackData
	);

	static HRESULT getFileDataCB(
		_In_ const PRJ_CALLBACK_DATA* callbackData,
		_In_ UINT64 byteOffset,
		_In_ UINT32 length
	);

	static HRESULT queryFileNameCallback(
		_In_ const PRJ_CALLBACK_DATA* callbackData
	);

	static HRESULT notificationCB(
		_In_ const PRJ_CALLBACK_DATA* callbackData,
		_In_ BOOLEAN isDirectory,
		_In_ PRJ_NOTIFICATION notificationType,
		_In_opt_ PCWSTR destinationFileName,
		_Inout_ PRJ_NOTIFICATION_PARAMETERS* notificationParameters
	);

	static void cancelCommandCB(
		_In_ const PRJ_CALLBACK_DATA* callbackData
	);

public:

	FileProvider();
	~FileProvider();

	void setVirtualizationPath(const WCHAR* path) { virtualization_path = path; }
	void setSourcePath(const WCHAR* path) { source_path = path; }
	const WCHAR* checkSanity();
	const WCHAR* startVirtualizing();

};