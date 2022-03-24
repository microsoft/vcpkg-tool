// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ValidationError } from './validation-error';


export interface Validation {
  /**
   * @internal
   *
   * actively validate this node.
  */
  validate(): Iterable<ValidationError>;
}
