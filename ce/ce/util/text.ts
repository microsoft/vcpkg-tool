// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { TextDecoder } from 'util';

const decoder = new TextDecoder('utf-8');

export function decode(input?: NodeJS.ArrayBufferView | ArrayBuffer | null | undefined) {
  return decoder.decode(input);
}
export function encode(content: string): Uint8Array {
  return Buffer.from(content, 'utf-8');
}

export function equalsIgnoreCase(s1: string | undefined, s2: string | undefined): boolean {
  return s1 === s2 || !!s1 && !!s2 && s1.localeCompare(s2, undefined, { sensitivity: 'base' }) === 0;
}
