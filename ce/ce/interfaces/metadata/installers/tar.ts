// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Strings } from '../../collections';
import { Installer } from './Installer';
import { UnpackSettings } from './unpack-settings';
import { Verifiable } from './verifiable';

/**
 * a file that can be untar'd
 *
 * combined with Verifiable, the hash should be matched before proceeding
 */

export interface UnTarInstaller extends Verifiable, UnpackSettings, Installer {
  /** the source location of a file to untar */
  location: Strings;
}
