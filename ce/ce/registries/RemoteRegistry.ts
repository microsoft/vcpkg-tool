// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { parse } from 'yaml';
import { ZipUnpacker } from '../archivers/ZipUnpacker';
import { Registry } from '../artifacts/registry';
import { registryIndexFile } from '../constants';
import { acquireArtifactFile } from '../fs/acquire';
import { i } from '../i18n';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { ArtifactIndex } from './artifact-index';
import { ArtifactRegistry } from './ArtifactRegistry';
import { Index } from './indexer';


export class RemoteRegistry extends ArtifactRegistry implements Registry {

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
      // if this is this a reference to a github repo, use the org/repo name
      this.#localName = /^https:\/\/github.com\/([a-zA-Z0-9-_]*\/[a-zA-Z0-9-_]*)\/?(.*)$/gi.exec(this.location.toString())?.[1];

      // if we didn't get a match, use the url to generate a local filesystem name
      if (!this.#localName) {
        this.#localName = this.location.toString().replace(/^[a-zA-Z0-9]*:\/*/g, '').replace(/[^a-zA-Z0-9/]/g, '_');
      }
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

  async update() {
    this.session.channels.message(i`Updating registry data from ${this.location.toString()}`);

    // get zip file location if
    const ref = /^https:\/\/github.com\/([a-zA-Z0-9-_]*\/[a-zA-Z0-9-_]*\/?)$/gi.exec(this.location.toString());
    let locations = [this.location];
    if (ref) {
      // it's just a github uri, let's use the main/m*ster branch as the zip file location.
      locations = [this.location.join('archive/refs/heads/main.zip'), this.location.join('archive/refs/heads/master.zip')];
    }

    const file = await acquireArtifactFile(this.session, [this.location], `${this.safeName}-registry.zip`, {});
    if (await file.exists()) {
      const unpacker = new ZipUnpacker(this.session);
      await unpacker.unpack(file, this.cacheFolder, {}, { strip: 1 });
      await file.delete();
    }
  }
}
