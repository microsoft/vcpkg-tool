// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Installer } from './Installer';
import { UnpackSettings } from './unpack-settings';
import { Verifiable } from './verifiable';

/**
 * a special version of UnZip, this assumes the nuget.org package service
 * the 'nupkg' value is the package id (ie, 'Microsoft.Windows.SDK.CPP.x64/10.0.19041.5')
 *
 * and that is appended to the known-url https://www.nuget.org/api/v2/package/ to get
 * the final url.
 *
 * post MVP we could add the ability to use artifact sources and grab the package that way.
 *
 * combined with Verifiable, the hash should be matched before proceeding
 */

export interface NupkgInstaller extends Verifiable, UnpackSettings, Installer {
  /** the source location of a file to unzip/untar/unrar/etc */
  location: string;
}
