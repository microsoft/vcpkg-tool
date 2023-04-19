// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ValidationMessage } from './validation-message';

export interface Validation {
  /**
   * @internal
   *
   * actively validate this node.
  */
  validate(): Iterable<ValidationMessage>;
}
