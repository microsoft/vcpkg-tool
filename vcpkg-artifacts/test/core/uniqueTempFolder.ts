// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { mkdtempSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

export function uniqueTempFolder(): string {
  return mkdtempSync(join(tmpdir(), '/ce-temp!'));
}
