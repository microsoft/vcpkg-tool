// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { SemVer } from 'semver';
import { MetadataFile } from '../amf/metadata-file';
import { IdentityKey, IndexSchema, SemverKey, StringKey } from './indexer';


export class ArtifactIndex extends IndexSchema<MetadataFile, ArtifactIndex> {
  id = new IdentityKey(this, (i) => i.id, ['IdentityKey/id', 'IdentityKey/info.id']);
  version = new SemverKey(this, (i) => new SemVer(i.version), ['SemverKey/version', 'SemverKey/info.version']);
  summary = new StringKey(this, (i) => i.summary, ['StringKey/summary', 'StringKey/info.summary']);
}
