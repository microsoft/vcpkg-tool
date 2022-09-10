// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Uri } from '../util/uri';

export interface HashVerifyEvents {
  hashVerifyStart(file: string): void;
  hashVerifyProgress(file: string, percent: number): void;
  hashVerifyComplete(file: string): void;
}

export interface DownloadEvents extends HashVerifyEvents {
  downloadStart(uris: Array<Uri>, destination: string): void;
  downloadProgress(uri: Uri, destination: string, percent: number): void;
}

export interface FileEntry {
  archiveUri: Uri;
  destination: Uri | undefined;
  path: string;
  extractPath: string | undefined;
}

export interface UnpackEvents {
  progress(archivePercentage: number): void;
  fileProgress(entry: Readonly<FileEntry>, filePercentage: number): void;
  unpacked(entry: Readonly<FileEntry>): void;
  error(entry: Readonly<FileEntry>, message: string): void;
}

export interface AcquireEvents extends DownloadEvents {
}

export interface InstallEvents extends DownloadEvents {
  // unpacking individual files
  fileProgress(entry: Readonly<FileEntry>, filePercentage: number): void;
  unpacked(entry: Readonly<FileEntry>): void;
  error(entry: Readonly<FileEntry>, message: string): void;

  // message when we have no ability to give a linear progress
  heartbeat(text: string): void;

  // overall progress events
  start(): void;
  progress(archivePercentage: number): void;
  complete(): void;
}

export interface InstallOptions {
  force?: boolean,
  allLanguages?: boolean,
  language?: string
}
