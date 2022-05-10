// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { createHash } from 'crypto';
import { parse } from 'yaml';
import { Registry } from '../artifacts/registry';
import { registryIndexFile } from '../constants';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { ArtifactRegistry } from './ArtifactRegistry';


export class LocalRegistry extends ArtifactRegistry implements Registry {
  protected indexYaml: Uri;
  readonly installationFolder;
  readonly cacheFolder: Uri;

  constructor(session: Session, location: Uri) {
    strict.ok(location.scheme === 'file', `local registry location must be a file uri (${location})`);

    super(session, location);
    this.cacheFolder = location;
    this.indexYaml = this.cacheFolder.join(registryIndexFile);
    this.installationFolder = session.installFolder.join(this.localName);
  }

  update(): Promise<void> {
    return this.regenerate();
  }

  override async load(force?: boolean): Promise<void> {
    if (force || !this.loaded) {
      if (! await this.indexYaml.exists()) {
        // generate an index from scratch
        await this.regenerate();
        this.loaded = true;
        return;
      }
      this.session.channels.debug(`Loading registry from '${this.indexYaml.fsPath}'`);
      this.index.deserialize(parse(await this.indexYaml.readUTF8()));
      this.loaded = true;
    }
  }

  private get localName() {
    // We use this to generate the subdirectory that we install artifacts into.
    // It's not reqired to be very unique, but we'll generate it based of the path of the local location.
    return createHash('sha256').update(this.location.fsPath, 'utf8').digest('hex').substring(0, 8);
  }
}
