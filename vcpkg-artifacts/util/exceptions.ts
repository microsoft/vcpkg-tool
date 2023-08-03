// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../i18n';
import { Uri } from './uri';

export class Failed extends Error {
  fatal = true;
}

export class RemoteFileUnavailable extends Error {
  constructor(public uri: Array<Uri>) {
    super();
  }
}

export class TargetFileCollision extends Error {
  constructor(public uri: Uri, message: string) {
    super(message);
  }
}

export class MultipleInstallsMatched extends Error {
  constructor(public queries: Array<string>) {
    super(i`Matched more than one install block [${queries.join(',')}]`);
  }
}

