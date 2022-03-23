// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { LineCounter } from 'yaml';
import { Session } from '../session';
import { Uri } from '../util/uri';


export interface DocumentContext {
  session: Session;
  filename: string;
  file: Uri;
  folder: Uri;
  lineCounter: LineCounter;
}