// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ErrorKind } from './error-kind';


export interface ValidationError {
  message: string;
  range?: [number, number, number] | { sourcePosition(): [number, number, number] | undefined };
  rangeOffset?: { line: number; column: number; };
  category: ErrorKind;
}
