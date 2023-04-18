// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { createHash } from 'crypto';
import { parse } from 'yaml';
import { unpackZip } from '../archivers/ZipUnpacker';
import { registryIndexFile } from '../constants';
import { acquireArtifactFile } from '../fs/acquire';
import { i } from '../i18n';
import { Session } from '../session';
import { isGithubRepo } from '../util/checks';
import { Uri } from '../util/uri';
import { ArtifactRegistry } from './ArtifactRegistry';
import { ArtifactIndex } from './artifact-index';
import { Index } from './indexer';

export class RemoteRegistry extends ArtifactRegistry {
  protected indexYaml: Uri;
  readonly installationFolder;
  readonly cacheFolder: Uri;
  #localName: string | undefined;

  constructor(session: Session, location: Uri) {
    strict.ok(location.scheme === 'https', `remote registry location must be an HTTPS uri (${location})`);
    super(session, location);

    this.cacheFolder = session.registryFolder.join(this.localName);
    this.indexYaml = this.cacheFolder.join(registryIndexFile);
    this.installationFolder = session.installFolder.join(this.localName);
  }

  /*
  notes:
    // does this look like a github repo (in which case assume '${url}/archive/refs/heads/main.zip') as the packed repo.
    // does this point to a .zip file ?
    // https://github.com/microsoft/vcpkg-ce-catalog/archive/refs/heads/main.zip
 */
  private get localName() {
    if (!this.#localName) {
      switch (this.location.authority.toLowerCase()) {
        case 'aka.ms':
          return this.#localName = this.location.path.replace(/\//g, '');

        case 'github.com':
          if (isGithubRepo(this.location)) {
            // it's a reference to a github repo, the assumption that the zip archive is what we're getting
            return this.#localName = this.location.path;
          }
          break;
      }
      // if we didn't get a match, use the url to generate a local filesystem name
      this.#localName = createHash('sha256').update(this.location.toString(), 'utf8').digest('hex').substring(0, 8);
    }
    return this.#localName;
  }

  private get safeName() {
    return this.localName.replace(/[^a-zA-Z0-9]/g, '.');
  }

  override async load(force?: boolean): Promise<void> {
    if (force || !this.loaded) {
      if (!await this.indexYaml.exists()) {
        await this.update();
      }

      strict.ok(await this.indexYaml.exists(), `Index file is missing '${this.indexYaml.fsPath}'`);

      // load it fresh.
      this.index = new Index(ArtifactIndex);

      this.session.channels.debug(`Loading registry from '${this.indexYaml.fsPath}'`);
      this.index.deserialize(parse(await this.indexYaml.readUTF8()));
      this.loaded = true;
    }
  }

  async update(displayName?: string) {
    if (!displayName) {
      displayName = this.location.toString();
    }

    this.session.channels.message(i`Updating registry data from ${displayName}`);

    let locations = [this.location];

    if (isGithubRepo(this.location)) {
      // it's just a github uri, let's use the main/m*ster branch as the zip file location.
      locations = [this.location.join('archive/refs/heads/main.zip'), this.location.join('archive/refs/heads/master.zip')];
    }

    const file = await acquireArtifactFile(this.session, locations, `${this.safeName}-registry.zip`, {}, {force: true});
    if (await file.exists()) {
      await unpackZip(this.session, file, this.cacheFolder, {}, { strip: -1 });
      await file.delete();
    }
  }
}
