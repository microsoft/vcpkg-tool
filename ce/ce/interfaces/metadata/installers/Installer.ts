// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Validation } from '../../validation';

/**
   * defines what should be physically laid out on disk for this artifact
   *
   * Note: once the host/environment queries have been completed, there should
   *       only be one single package/file/repo/etc that gets downloaded and
   *       installed for this artifact.  If there needs to be more than one,
   *       then there would need to be a 'requires' that refers to the additional
   *       package.
   *
   * More types to follow.
   */

export interface Installer extends Validation {
  readonly installerKind: string;
  readonly lang?: string; // note to only install this entry when the current locale is this language
  readonly nametag?: string; // note to include this tag in the file name of the cached artifact
}
