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
  destination: Uri;
  path: string;
  extractPath: string | undefined;
}

export interface UnpackEvents {
  unpackArchiveStart(archiveUri: Uri, outputUri: Uri): void;
  unpackArchiveProgress(archiveUri: Uri, archivePercentage: number): void;
  // message when we have no ability to give a linear progress
  unpackArchiveHeartbeat(text: string): void;
  unpackFileProgress(entry: Readonly<FileEntry>, filePercentage: number): void;
  unpackFileComplete(entry: Readonly<FileEntry>): void;
}

export interface InstallEvents extends DownloadEvents, UnpackEvents {
  startInstallArtifact(artifactDisplayName: string): void;
  alreadyInstalledArtifact(artifactDisplayName: string): void;
}

export interface InstallOptions {
  force?: boolean,
  allLanguages?: boolean,
  language?: string
}
