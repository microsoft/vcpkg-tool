// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Strings } from '../../collections';
import { Validation } from '../../validation';

export interface Registry {
  readonly registryKind?: string;
}

export interface ArtifactRegistry extends Registry, Validation {
  /** the uri to the artifact source location */
  readonly location: Strings;
}
