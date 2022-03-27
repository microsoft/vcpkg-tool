// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MetadataFile } from '../amf/metadata-file';
import { Session } from '../session';
import { Uri } from '../util/uri';

export const THIS_IS_NOT_A_MANIFEST_ITS_AN_INDEX_STRING = '# MANIFEST-INDEX';

export async function isIndexFile(uri: Uri): Promise<boolean> {
  try {
    return (await uri.isFile()) && (await uri.readUTF8()).startsWith(THIS_IS_NOT_A_MANIFEST_ITS_AN_INDEX_STRING);
  } catch {
    return false;
  }
}
export async function isMetadataFile(uri: Uri, session: Session): Promise<boolean> {
  if (await uri.isFile()) {
    try {
      return (await MetadataFile.parseMetadata(uri, session))?.info?.exists();
    } catch {
      // nope. no worries.
    }
  }
  return false;
}

