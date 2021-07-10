/**
 * @file Driver entry point
 *
 * Copyright (c) 2021 Pedro Falcato All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "Ext4.h"

EFI_STATUS
Ext4OpenPartition (
  EFI_HANDLE DeviceHandle, EFI_DISK_IO_PROTOCOL *diskIo,
  EFI_DISK_IO2_PROTOCOL *diskIo2, EFI_BLOCK_IO_PROTOCOL *blockIo
  )
{
  EXT4_PARTITION  *Part = AllocateZeroPool (sizeof (*Part));

  if(Part == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Part->BlockIo = blockIo;
  Part->DiskIo  = diskIo;
  Part->DiskIo2 = diskIo2;

  EFI_STATUS  st = Ext4OpenSuperblock (Part);

  if(EFI_ERROR (st)) {
    FreePool (Part);
    return st;
  }

  Part->Interface.Revision   = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
  Part->Interface.OpenVolume = Ext4OpenVolume;
  st = gBS->InstallMultipleProtocolInterfaces (
              &DeviceHandle,
              &gEfiSimpleFileSystemProtocolGuid,
              &Part->Interface,
              NULL
              );

  if(EFI_ERROR (st)) {
    FreePool (Part);
    return st;
  }

  return EFI_SUCCESS;
}

VOID
Ext4SetupFile (
  IN OUT EXT4_FILE *File, EXT4_PARTITION *Partition
  )
{
  // TODO: Support revision 2 (needs DISK_IO2 + asynchronous IO)
  File->Protocol.Revision    = EFI_FILE_PROTOCOL_REVISION;
  File->Protocol.Open        = Ext4Open;
  File->Protocol.Close       = Ext4Close;
  File->Protocol.Delete      = Ext4Delete;
  File->Protocol.Read        = Ext4ReadFile;
  File->Protocol.Write       = Ext4WriteFile;
  File->Protocol.SetPosition = Ext4SetPosition;
  File->Protocol.GetPosition = Ext4GetPosition;
  File->Protocol.GetInfo     = Ext4GetInfo;

  File->Partition = Partition;
}
