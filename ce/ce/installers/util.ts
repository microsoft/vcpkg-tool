// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { AcquireOptions } from '../fs/acquire';
import { Installer } from '../interfaces/metadata/installers/Installer';
import { Verifiable } from '../interfaces/metadata/installers/verifiable';

export function artifactFileName(name: string, install: Installer, extension: string): string {
  let result = name;
  if (install.nametag) {
    result += '-';
    result += install.nametag;
  }

  if (install.lang) {
    result += '-';
    result += install.lang;
  }

  result += extension;
  return result.replace(/[^\w]+/g, '.');
}

export function applyAcquireOptions(options: AcquireOptions, install: Verifiable): AcquireOptions {
  if (install.sha256) {
    return { ...options, algorithm: 'sha256', value: install.sha256 };
  }
  if (install.sha512) {
    return { ...options, algorithm: 'sha512', value: install.sha512 };
  }

  return options;
}

