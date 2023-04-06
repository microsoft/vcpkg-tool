// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Strings } from '../../collections';
import { Installer } from './Installer';
import { UnpackSettings } from './unpack-settings';
import { Verifiable } from './verifiable';

/**
 * a file that can be unzipp'd
 *
 * combined with Verifiable, the hash should be matched before proceeding
 */

export interface UnZipInstaller extends Verifiable, UnpackSettings, Installer {
  /** the source location of a file to unzip */
  location: Strings;
}
