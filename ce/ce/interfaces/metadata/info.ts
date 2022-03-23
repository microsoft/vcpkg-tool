// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Validation } from '../validation';

/** Canonical Information about this artifact */

export interface Info extends Validation {
  /** Artifact identity
   *
   * this should be the 'path' to the artifact (following the guidelines)
   *
   * ie, 'compilers/microsoft/msvc'
   *
   * FYI: artifacts install to $VCPKG_ROOT/<id>/<VER> or if from another artifact source: $VCPKG_ROOT/<source>/<id>/<VER>
   */
  id: string;

  /** the version of this artifact */
  version: string;

  /** a short 1 line descriptive text */
  summary?: string;

  /** if a longer description is required, the value should go here */
  description?: string;

  /** if true, intended to be used only as a dependency; for example, do not show in search results or lists */
  dependencyOnly: boolean;

  /** higher priority artifacts should install earlier */
  priority?: number;
}
