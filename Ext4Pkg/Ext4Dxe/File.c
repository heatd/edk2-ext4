/**
 * @file EFI_FILE_PROTOCOL implementation for EXT4
 * 
 * @copyright Copyright (c) 2021 Pedro Falcato
 * 
 */

#include "Ext4.h"
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>

static EFI_STATUS GetPathSegment(IN const CHAR16 *Path, OUT CHAR16 *PathSegment, OUT UINTN *Length)
{
  const CHAR16 *Start = Path;
  const CHAR16 *End = Path;
    
  // The path segment ends on a backslash or a null terminator
  for(; *End != L'\0' && *End != L'\\'; End++)
  {}

  *Length = End - Start;

  return StrnCpyS(PathSegment, EXT4_NAME_MAX, Start, End - Start);
}

#define EXT4_INO_PERM_READ_OWNER   0400
#define EXT4_INO_PERM_WRITE_OWNER  0200

BOOLEAN Ext4ApplyPermissions(IN OUT EXT4_FILE *File, UINT64 OpenMode)
{
  UINT16 NeededPerms = 0;

  if(OpenMode & EFI_FILE_MODE_READ)
    NeededPerms |= EXT4_INO_PERM_READ_OWNER;
  
  if(OpenMode & EFI_FILE_MODE_WRITE)
    NeededPerms |= EXT4_INO_PERM_WRITE_OWNER;
  
  if((File->Inode->i_mode & NeededPerms) != NeededPerms)
    return FALSE;
  
  File->OpenMode = OpenMode;

  return TRUE;
}

EFI_STATUS EFIAPI Ext4Open(
  IN EFI_FILE_PROTOCOL        *This,
  OUT EFI_FILE_PROTOCOL       **NewHandle,
  IN CHAR16                   *FileName,
  IN UINT64                   OpenMode,
  IN UINT64                   Attributes
  )
{
  EXT4_FILE *Current = (EXT4_FILE *) This;
  EXT4_PARTITION *Partition = Current->Partition;
  UINTN Level = 0;

  DEBUG((EFI_D_INFO, "[ext4] Ext4Open %s\n", FileName));
  // If the path starts with a backslash, we treat the root directory as the base directory
  if(FileName[0] == L'\\')
  {
    FileName++;
    Current = Partition->Root;
  }

  while(FileName[0] != L'\0')
  {
    // Discard leading path separators
    while(FileName[0] == L'\\')
        FileName++;

    CHAR16 PathSegment[EXT4_NAME_MAX + 1];
    UINTN Length;
    if(GetPathSegment(FileName, PathSegment, &Length) != EFI_SUCCESS)
        return EFI_BUFFER_TOO_SMALL;

    // Reached the end of the path
    if(Length == 0)
        break;

    FileName += Length;

    DEBUG((EFI_D_INFO, "[ext4] Opening %s\n", PathSegment));

    EXT4_FILE *File;

    // TODO: We should look at the execute bit for permission checking on directory lookups
    // ^^ This would require a better knowledge of the path itself since we would need to know whether or not
    // we're the last token.

    // TODO: Symlinks?
    EFI_STATUS st = Ext4OpenFile(Current, PathSegment, Partition, EFI_FILE_MODE_READ, &File);

    if(EFI_ERROR(st) && st != EFI_NOT_FOUND)
    {
        return st;
    }
    else if(st == EFI_NOT_FOUND)
    {
      // TODO: Handle file creation
        return st;
    }

    // Check if this is a valid file to open in EFI
    if(!Ext4FileIsOpenable(File))
    {
      Ext4CloseInternal(File);
      // TODO: What status should we return?
      return EFI_ACCESS_DENIED;
    }

    if(Level != 0)
    {
      // Careful not to close the base directory
      Ext4CloseInternal(Current);
    }

    Level++;

    Current = File;
  }

  if(!Ext4ApplyPermissions(Current, OpenMode))
  {
    Ext4CloseInternal(Current);
    return EFI_ACCESS_DENIED;
  }

  *NewHandle = &Current->Protocol;

  DEBUG((EFI_D_INFO, "Open successful\n"));
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI Ext4Close(
  IN EFI_FILE_PROTOCOL *This
  )
{
  return Ext4CloseInternal((EXT4_FILE *) This);
}

EFI_STATUS Ext4CloseInternal(
  IN EXT4_FILE *File
  )
{
  if(File == File->Partition->Root)
    return EFI_SUCCESS;

  DEBUG((EFI_D_INFO, "[ext4] Closed file %p (inode %lu)\n", File, File->InodeNum));
  FreePool(File->Inode);
  FreePool(File);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI Ext4Delete(
  IN EFI_FILE_PROTOCOL  *This
  )
{
  // TODO: When we add write support
  Ext4Close(This);
  return EFI_WARN_DELETE_FAILURE;
}

EFI_STATUS
EFIAPI Ext4ReadFile(
  IN EFI_FILE_PROTOCOL        *This,
  IN OUT UINTN                *BufferSize,
  OUT VOID                    *Buffer
  )
{
  EXT4_FILE *File = (EXT4_FILE *) This;
  EXT4_PARTITION *Partition = File->Partition;

  ASSERT(Ext4FileIsOpenable(File));

  if(Ext4FileIsReg(File))
  {
    UINT64 Length = *BufferSize;
    EFI_STATUS st = Ext4Read(Partition, File->Inode, Buffer, File->Position, &Length);
    if(st == EFI_SUCCESS)
    {
      *BufferSize = (UINTN) Length;
      File->Position += Length;
    }

    return st; 
  }
  else if(Ext4FileIsDir(File))
  {
    // TODO: Implement Ext4ReadDir
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI Ext4WriteFile(
  IN EFI_FILE_PROTOCOL        *This,
  IN OUT UINTN                *BufferSize,
  IN VOID                    *Buffer
  )
{
  EXT4_FILE *File = (EXT4_FILE *) This;

  if(!(File->OpenMode & EFI_FILE_MODE_WRITE))
    return EFI_ACCESS_DENIED;

  // TODO: Add write support
  return EFI_WRITE_PROTECTED;
}

EFI_STATUS
EFIAPI Ext4GetPosition(
  IN EFI_FILE_PROTOCOL        *This,
  OUT UINT64                  *Position
  )
{
  EXT4_FILE *File = (EXT4_FILE *) This;
  if(Ext4FileIsDir(File))
    return EFI_UNSUPPORTED;
  
  *Position = File->Position;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI Ext4SetPosition(
  IN EFI_FILE_PROTOCOL        *This,
  IN UINT64                   Position
  )
{
  EXT4_FILE *File = (EXT4_FILE *) This;
  // Only seeks to 0 (so it resets the ReadDir operation) are allowed
  if(Ext4FileIsDir(File) && Position != 0)
    return EFI_UNSUPPORTED;
  
  // -1 (0xffffff.......) seeks to the end of the file
  if(Position == (UINT64) -1)
  {
    Position = EXT4_INODE_SIZE(File->Inode);
  }

  File->Position = Position;

  return EFI_SUCCESS;
}

static EFI_STATUS Ext4GetFileInfo(IN EXT4_FILE *File, OUT EFI_FILE_INFO *Info, IN OUT UINTN *BufferSize)
{
  // TODO: Get a way to get and set the directory entry
  if(*BufferSize < sizeof(EFI_FILE_INFO))
  {
    *BufferSize = sizeof(EFI_FILE_INFO);
    return EFI_BUFFER_TOO_SMALL;
  }

  Info->FileSize = EXT4_INODE_SIZE(File->Inode);
  Info->PhysicalSize = Ext4FilePhysicalSpace(File);
  Ext4FileATime(File, &Info->LastAccessTime);
  Ext4FileMTime(File, &Info->ModificationTime);
  Ext4FileCreateTime(File, &Info->LastAccessTime);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI Ext4GetInfo(
  IN EFI_FILE_PROTOCOL        *This,
  IN EFI_GUID                 *InformationType,
  IN OUT UINTN                *BufferSize,
  OUT VOID                    *Buffer
  )
{
  if(CompareGuid(InformationType, &gEfiFileInfoGuid))
    return Ext4GetFileInfo((EXT4_FILE *) This, Buffer, BufferSize);
  
  return EFI_UNSUPPORTED;
}
