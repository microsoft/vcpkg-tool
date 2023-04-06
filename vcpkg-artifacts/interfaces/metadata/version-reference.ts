// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Range, SemVer } from 'semver';
import { Validation } from '../validation';


export interface VersionReference extends Validation {
  range: Range;
  resolved?: SemVer;
  readonly raw?: string;
}
