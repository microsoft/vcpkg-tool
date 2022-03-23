// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/** One of several choices for a HASH etc */

export interface Verifiable {
  /** SHA-256 hash */
  sha256?: string;
  sha512?: string;
}
