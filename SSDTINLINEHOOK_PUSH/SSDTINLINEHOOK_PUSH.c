#include "SSDTINLINEHOOK_PUSH.h"
#include "Common.h"
extern PSYSTEM_SERVICE_DESCRIPTOR_TABLE KeServiceDescriptorTable;
extern POBJECT_TYPE* PsProcessType;
extern POBJECT_TYPE* IoFileObjectType;
PVOID* __ServiceTableBase = NULL;
LPFN_OBGETOBJECTTYPE __ObGetObjectType = NULL;
LPFN_NTOPENPROCESS __NtOpenProcess = NULL;
UCHAR*   __OriginalNtOpenProcessCode = NULL;
UCHAR*   __TrampolineCode = NULL;
ULONG    __PatchedCodeLength = 5;
PVOID pProxyFunction = 0;
PVOID HookSSDTFunctionByPush(PVOID pSourceFunction, PVOID FakeAddress);
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegisterPath)
{

	UNREFERENCED_PARAMETER(RegisterPath);

	ULONG	NtOpenProcessIndex = 0;
	CHAR    ZwFunctionName[] = "ZwOpenProcess";


	NTSTATUS  Status = STATUS_UNSUCCESSFUL;


	
	DbgPrint("DriverEntry()\r\n");

	//设置驱动卸载历程
	DriverObject->DriverUnload = DriverUnload;


	//获取SSDT
	__ServiceTableBase = (PVOID*)KeServiceDescriptorTable->ServiceTableBase;
	 
	if (__ServiceTableBase == NULL)
	{
		return Status;
	}
	//获取NtXXX函数的索引
	if (!NT_SUCCESS(SeGetSSDTFunctionIndexByFunctionName(ZwFunctionName,
		&NtOpenProcessIndex)))
	{
		return Status;
	}
	/*
	3: kd> u NtOpenProcess
	nt!NtOpenProcess:
	84a53ba1 8bff            mov     edi,edi
	84a53ba3 55              push    ebp
	84a53ba4 8bec            mov     ebp,esp
	84a53ba6 51              push    ecx
	84a53ba7 51              push    ecx
	84a53ba8 64a124010000    mov     eax,dword ptr fs:[00000124h]
	84a53bae 8a803a010000    mov     al,byte ptr [eax+13Ah]
	84a53bb4 8b4d14          mov     ecx,dword ptr [ebp+14h]


	*/

	DbgBreakPoint();
	__NtOpenProcess = (LPFN_NTOPENPROCESS)(__ServiceTableBase[NtOpenProcessIndex]);
	if (__NtOpenProcess == NULL)
	{
		return Status;
	}

 
	HookSSDTFunctionByPush(__NtOpenProcess, FakeNtOpenProcess);

	return STATUS_SUCCESS;
}

 
PVOID HookSSDTFunctionByPush(PVOID pSourceFunction, PVOID FakeAddress)
{

	 

	UCHAR JumpCode[6] = { 0x68,0x00,0x00,0x00,0x00,0xC3 };     //push xxxxxxxx ret
	UCHAR JumpBackCode[6] = { 0x68,0x00,0x00,0x00,0x00,0xC3 }; //push xxxxxxxx ret

	 
	if (!pSourceFunction)return NULL;

	*(ULONG *)((ULONG)JumpCode + 1) = (ULONG)FakeAddress;


	 
	PUCHAR pOpCode;
	ULONG BackupLength = 0;


	while (BackupLength < 6)
	{
		BackupLength += GetFunctionCodeSize((PVOID)((ULONG)pSourceFunction + BackupLength), &pOpCode);
	}
	pProxyFunction = ExAllocatePool(NonPagedPool, (BackupLength + 6));
	
	if (!pProxyFunction)return NULL;

	*(ULONG *)((ULONG)JumpBackCode + 1) = (ULONG)pSourceFunction + BackupLength;

	RtlCopyMemory(pProxyFunction, pSourceFunction, BackupLength);
	RtlCopyMemory((PVOID)((ULONG)pProxyFunction + BackupLength), JumpBackCode, 6);


	

	
	OnEnableWrite();
	RtlCopyMemory(pSourceFunction, JumpCode, 6);
	OnDisableWrite();
	

	

	return pProxyFunction;


}

VOID SSDTUninlineHook(PVOID OriginalAddress, PUCHAR OriginalCode, ULONG PatchedCodeLength)
{
	OnEnableWrite();
	memcpy(OriginalAddress, pProxyFunction , 6);
	OnDisableWrite();
}
NTSTATUS SeGetSSDTFunctionIndexByFunctionName(CHAR* ZwFunctionName, ULONG* NtFunctionIndex)
{
	/*
	0:004> u zwopenProcess
	ntdll!ZwOpenProcess:
	77845dc8 b8be000000      mov     eax,0BEh
	77845dcd ba0003fe7f      mov     edx,offset SharedUserData!SystemCallStub (7ffe0300)
	77845dd2 ff12            call    dword ptr [edx]
	77845dd4 c21000          ret     10h
	77845dd7 90              nop

	*/

	ULONG     Offset = 1;
	WCHAR     FileFullPath[] = L"\\SystemRoot\\System32\\ntdll.dll";
	NTSTATUS  Status = STATUS_SUCCESS;
	SIZE_T    MappedFileSize = 0;
	PVOID     MappedFileVA = NULL;
	PIMAGE_NT_HEADERS  ImageNtHeaders = NULL;
	PIMAGE_EXPORT_DIRECTORY ImageExoportDirectory = NULL;
	ULONG*    AddressOfFunctions = NULL;
	ULONG*    AddressOfNames = NULL;
	USHORT*   AddressOfNameOrdinals = NULL;
	CHAR*     FunctionName = NULL;
	ULONG     i = 0;
	PUCHAR    FunctionAddress = 0;

	//将Ntdll.dll文件映射到系统空间中
	Status = SeMappingPEFileInRing0Space(FileFullPath, &MappedFileVA, &MappedFileSize);
	if (!NT_SUCCESS(Status))
	{
		return Status;
	}
	else
	{
		__try {
			//通过DosHead获得NtHeaders
			ImageNtHeaders = RtlImageNtHeader(MappedFileVA);
			if (ImageNtHeaders && ImageNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress)
			{
				ImageExoportDirectory = (IMAGE_EXPORT_DIRECTORY*)((ULONG_PTR)MappedFileVA +
					ImageNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);



				AddressOfFunctions = (ULONG*)((ULONG_PTR)MappedFileVA + ImageExoportDirectory->AddressOfFunctions);
				AddressOfNames = (ULONG*)((ULONG_PTR)MappedFileVA + ImageExoportDirectory->AddressOfNames);
				AddressOfNameOrdinals = (USHORT*)((ULONG_PTR)MappedFileVA + ImageExoportDirectory->AddressOfNameOrdinals);

				for (i = 0; i < ImageExoportDirectory->NumberOfNames; i++)
				{
					FunctionName = (char*)((ULONG_PTR)MappedFileVA + AddressOfNames[i]);   //获得函数名称
					if (_stricmp(FunctionName, ZwFunctionName) == 0)
					{
						FunctionAddress = (PUCHAR)((ULONG_PTR)MappedFileVA +
							AddressOfFunctions[AddressOfNameOrdinals[i]]);


						*NtFunctionIndex = *(ULONG*)(FunctionAddress + Offset);
						break;
					}
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			;
		}
	}
	ZwUnmapViewOfSection(NtCurrentProcess(), MappedFileVA);


	if (*NtFunctionIndex == -1)
	{
		Status = STATUS_UNSUCCESSFUL;
	}

	return Status;
}
NTSTATUS
SeMappingPEFileInRing0Space(WCHAR* FileFullPath, OUT PVOID* MappedFileVA, PSIZE_T MappedFileSize)
{
	UNICODE_STRING    v1;
	OBJECT_ATTRIBUTES ObjectAttributes;
	NTSTATUS          Status = STATUS_SUCCESS;
	IO_STATUS_BLOCK   IoStatusBlock;

	HANDLE   FileHandle = NULL;
	HANDLE   SectionHandle = NULL;

	if (!FileFullPath || !MappedFileVA)
	{
		return STATUS_UNSUCCESSFUL;
	}
	RtlInitUnicodeString(&v1, FileFullPath);
	InitializeObjectAttributes(&ObjectAttributes,
		&v1,
		OBJ_CASE_INSENSITIVE,
		NULL,
		NULL
	);
	//获得文件句柄
	Status = ZwCreateFile(&FileHandle,
		SYNCHRONIZE,
		&ObjectAttributes,
		&IoStatusBlock,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ,
		FILE_OPEN,
		FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0);
	if (!NT_SUCCESS(Status))
	{

		return Status;
	}

	//创建一个映射对象
	ObjectAttributes.ObjectName = NULL;
	Status = ZwCreateSection(&SectionHandle,
		SECTION_QUERY | SECTION_MAP_READ,
		&ObjectAttributes,
		NULL,
		PAGE_WRITECOPY,             //写拷贝
		SEC_IMAGE,                  //指示内存对齐
		FileHandle
	);
	ZwClose(FileHandle);
	if (!NT_SUCCESS(Status))
	{
		return Status;
	}
	Status = ZwMapViewOfSection(SectionHandle,
		NtCurrentProcess(),    //映射到当前进程的内存空间中
		MappedFileVA,
		0,
		0,
		0,
		MappedFileSize,
		ViewUnmap,
		0,
		PAGE_WRITECOPY
	);
	ZwClose(SectionHandle);
	if (!NT_SUCCESS(Status))
	{
		return Status;
	}

	return Status;
}

NTSTATUS
FakeNtOpenProcess(
	OUT PHANDLE ProcessHandle,
	IN ACCESS_MASK DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes,
	IN PCLIENT_ID ClientId OPTIONAL
)
{
	__try
	{
		//进程上下背景文
		PEPROCESS  EProcess = PsGetCurrentProcess();
		if (EProcess != NULL&&MmIsAddressValid(EProcess) && SeIsRealProcess(EProcess) == TRUE)
		{
			//通过EProcess获得进程名称 
			WCHAR  ProcessFullPath[MAX_PATH] = { 0 };
			if (SeGetProcessFullPathByEProcess(EProcess, ProcessFullPath, MAX_PATH) == TRUE)
			{
				DbgPrint("%S\r\n", ProcessFullPath);
				if (wcsstr(ProcessFullPath, L"1.exe") != 0)
				{
					return STATUS_ACCESS_DENIED;  //黑名单
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}

	if (__TrampolineCode != NULL)  //  PUSH 不会走到这里面来
	{
		return ((LPFN_NTOPENPROCESS)__TrampolineCode)(ProcessHandle, DesiredAccess, ObjectAttributes,
			ClientId);  //白名单
	}
}
BOOLEAN SeGetProcessFullPathByEProcess(PEPROCESS EProcess, WCHAR* ProcessFullPath, ULONG ProcessFullPathLength)
{
	BOOLEAN IsOk = FALSE;
	KPROCESSOR_MODE PreviousMode;
	HANDLE ProcessHandle = NULL;
	ULONG HandleAttributes = 0;


	PreviousMode = PsGetCurrentThreadPreviousMode();
	//句柄都是4的倍数  0x80000004   0x00000004
	HandleAttributes = (PreviousMode == KernelMode ? OBJ_KERNEL_HANDLE : 0);
	//通过对象体获得对象句柄
	if (NT_SUCCESS(ObOpenObjectByPointer(EProcess, HandleAttributes, NULL, PROCESS_QUERY_INFORMATION,
		*PsProcessType, PreviousMode, &ProcessHandle)))
	{
		PVOID BufferData = NULL;
		ULONG ReturnLength = 0;

		if (ZwQueryInformationProcess(ProcessHandle, ProcessImageFileName,
			BufferData, ReturnLength, &ReturnLength) == STATUS_INFO_LENGTH_MISMATCH)
		{
			if (BufferData = ExAllocatePool(PagedPool, ReturnLength))
			{
				if (NT_SUCCESS(ZwQueryInformationProcess(ProcessHandle,
					ProcessImageFileName, BufferData, ReturnLength, &ReturnLength)))
				{
					HANDLE FileHandle = NULL;
					OBJECT_ATTRIBUTES ObjectAttributes;
					IO_STATUS_BLOCK IoStatusBlock;

					InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)BufferData,
						OBJ_CASE_INSENSITIVE | HandleAttributes, NULL, NULL);
					if (NT_SUCCESS(ZwOpenFile(&FileHandle, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
						&ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT)))
					{
						PFILE_OBJECT FileObject;

						//通过句柄获得对象
						if (NT_SUCCESS(ObReferenceObjectByHandle(FileHandle, FILE_READ_ATTRIBUTES,
							*IoFileObjectType, PreviousMode, (PVOID*)&FileObject, NULL)))
						{
							POBJECT_NAME_INFORMATION ObjetNameInfo;

							//通过文件对象获得文件绝对路径
							if (NT_SUCCESS(IoQueryFileDosDeviceName(FileObject, &ObjetNameInfo)))
							{

								if (((UNICODE_STRING*)ObjetNameInfo)->MaximumLength < ProcessFullPathLength)
								{
									memcpy(ProcessFullPath, ((UNICODE_STRING*)ObjetNameInfo)->Buffer,
										((UNICODE_STRING*)ObjetNameInfo)->MaximumLength);
								}
								else
								{
									memcpy(ProcessFullPath, ((UNICODE_STRING*)ObjetNameInfo)->Buffer, ProcessFullPathLength);
								}
								IsOk = TRUE;
							}
							ObDereferenceObject(FileObject);
						}
						ZwClose(FileHandle);
					}
				}
				ExFreePool(BufferData);
			}
		}
		ZwClose(ProcessHandle);
	}
	return IsOk;
}
BOOLEAN SeIsRealProcess(PEPROCESS EProcess)
{
	//查看EProcess是否具有进程对象特征
	ULONG_PTR    ObjectType;
	ULONG_PTR    ObjectTypeAddress;
	ULONG_PTR    ProcessType = ((ULONG_PTR)*PsProcessType);   //系统第一模块导出的全局变量
															  /*
															  dd PsProcessType
															  849aa104  878dcd28
															  */

															  //从系统中的第一个模块(ntkrnlpa.exe)中的导出表中获得函数地址

	if (__ObGetObjectType == NULL)
	{
		UNICODE_STRING v1;
		RtlInitUnicodeString(&v1, L"ObGetObjectType");
		__ObGetObjectType = (LPFN_OBGETOBJECTTYPE)MmGetSystemRoutineAddress(&v1);
	}
	/*
	0: kd> dd __ObGetObjectType
	a8189008  84a99b68 00000000 00000000 00000000
	0: kd> u  84a99b68
	nt!ObGetObjectType:
	84a99b68 8bff            mov     edi,edi
	84a99b6a 55              push    ebp
	84a99b6b 8bec            mov     ebp,esp
	84a99b6d 8b4508          mov     eax,dword p
	*/
	if (ProcessType && EProcess && MmIsAddressValid((PVOID)(EProcess)))
	{

		ObjectType = __ObGetObjectType((PVOID)EProcess);
		if (ObjectType &&
			ProcessType == ObjectType)
		{
			//当前EProcess是一个有效的进程
			return TRUE;
		}


	}
	return FALSE;
}


 
void DriverUnload(PDRIVER_OBJECT DriverObject)
{
	SSDTUninlineHook(__NtOpenProcess, __OriginalNtOpenProcessCode, __PatchedCodeLength);


	if (__OriginalNtOpenProcessCode != NULL)
	{
		ExFreePool(__OriginalNtOpenProcessCode);
		__OriginalNtOpenProcessCode = NULL;
	}

	if (__TrampolineCode != NULL)
	{
		ExFreePool(__TrampolineCode);
		__TrampolineCode = NULL;
	}
	DbgPrint("DriverUnload()\r\n");
}
//关闭写保护
VOID
OnEnableWrite()
{
	__try
	{
		_asm
		{
			cli                    //禁止中断发生
			mov eax, cr0
			and eax, not 10000h    //cr0寄存器中第17位 WP位 
			mov cr0, eax
		}
	}
	__except (1)
	{

	}
}

//恢复写保护
VOID
OnDisableWrite()
{
	__try
	{
		_asm
		{
			mov eax, cr0
			or eax, 10000h
			mov cr0, eax

			sti                    //允许中断发生 
		}
	}
	__except (1)
	{

	}
}
