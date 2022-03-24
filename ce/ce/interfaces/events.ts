// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Uri } from '../util/uri';

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
export interface Progress {
  progress(percent: number, bytes: number, msec: number): void;
}

export interface AcquireEvents extends Progress {
  download(file: string, percent: number): void;
  verifying(file: string, percent: number): void;
  complete(): void;
}

/** The event definitions for for unpackers */
export interface UnpackEvents {
  progress(archivePercentage: number): void;
  fileProgress(entry: Readonly<FileEntry>, filePercentage: number): void;
  unpacked(entry: Readonly<FileEntry>): void;
  error(entry: Readonly<FileEntry>, message: string): void;
}

export interface FileEntry {
  archiveUri: Uri;
  destination: Uri | undefined;
  path: string;
  extractPath: string | undefined;
}

export interface InstallEvents {
  // download/verifying
  download(file: string, percent: number): void;
  verifying(file: string, percent: number): void;

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