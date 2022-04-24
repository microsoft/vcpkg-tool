// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail } from 'assert';
import { createHash } from 'crypto';
import { Readable } from 'stream';
import { ProgressTrackingStream } from '../fs/streams';
import { Uri } from './uri';

// sha256, sha512, sha384
export type Algorithm = 'sha256' | 'sha384' | 'sha512'

export async function hash(stream: Readable, uri: Uri, size: number, algorithm: 'sha256' | 'sha384' | 'sha512' = 'sha256', events: Partial<VerifyEvents>) {
  stream = await stream;

  try {
    const p = new ProgressTrackingStream(0, size);
    p.on('progress', (filePercentage) => events.verifying?.(uri.fsPath, filePercentage));

    for await (const chunk of stream.pipe(p).pipe(createHash(algorithm)).setEncoding('hex')) {
      // it should be done reading here
      return chunk;
    }
  } finally {
    stream.destroy();
  }
  fail('Should have returned a chunk from the pipe');
}

export interface VerifyEvents {
  verifying(file: string, percent: number): void;
}

export interface Hash {
  value?: string;
  algorithm?: 'sha256' | 'sha384' | 'sha512'
}
