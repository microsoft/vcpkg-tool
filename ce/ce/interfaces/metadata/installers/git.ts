// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Installer } from './Installer';

export interface CloneSettings {
  /** optionally, a tag/branch to be checkout out */
  commit?: string;

  /**
   * determines if the whole repo is cloned.
   *
   * Note:
   *  - when false (default), indicates that the repo should be cloned with --depth 1
   *  - when true, indicates that the full repo should be cloned
   * */
  full?: boolean;

  /**
   * determines if the repo should be cloned recursively.
   *
   * Note:
   *  - when false (default), indicates that the repo should clone recursive submodules
   *  - when true, indicates that the repo should be cloned recursively.
   */
  recurse?: boolean;

  /**
   * Gives a subdirectory to clone the repo to, if given.
   */
  subdirectory?: string;
}

/**
 * Installer that clones a git repository
 */
export interface GitInstaller extends Installer, CloneSettings {
  /** the git repo location to be cloned */
  location: string;

  /**
   * determines if the thing being installed is esp-idf, and if so, it should do some other installations after
   * the git install.
   */
  espidf?: boolean;
}
