// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { compare } from 'semver';
import { MetadataFile } from '../amf/metadata-file';
import { Artifact } from '../artifacts/artifact';
import { Registry, SearchCriteria } from '../artifacts/registry';
import { FileType } from '../fs/filesystem';
import { Session } from '../session';
import { Queue } from '../util/promise';
import { Uri } from '../util/uri';
import { isYAML, serialize } from '../yaml/yaml';
import { ArtifactIndex } from './artifact-index';
import { Index } from './indexer';
import { Registries } from './registries';
import { THIS_IS_NOT_A_MANIFEST_ITS_AN_INDEX_STRING } from './standard-registry';


export abstract class ArtifactRegistry implements Registry {
  constructor(protected session: Session, readonly location: Uri) {
  }
  abstract load(): Promise<void>;

  abstract readonly installationFolder: Uri;

  protected abstract readonly cacheFolder: Uri;
  protected index = new Index(ArtifactIndex);
  protected abstract indexYaml: Uri;

  get count() {
    return this.index.indexOfTargets.length;
  }

  #loaded = false;

  get loaded() {
    return this.#loaded;
  }

  protected set loaded(loaded: boolean) {
    this.#loaded = loaded;
  }

  abstract update(): Promise<void>;

  async regenerate(normalize?: boolean): Promise<void> {
    // reset the index to blank.
    this.index = new Index(ArtifactIndex);

    const repo = this;
    const q = new Queue();
    const session = this.session;

    async function processFile(uri: Uri) {

      const content = await uri.readUTF8();
      // if you see this, it's an index, and we can skip even trying.
      if (content.startsWith(THIS_IS_NOT_A_MANIFEST_ITS_AN_INDEX_STRING)) {
        return;
      }
      try {
        const amf = await MetadataFile.parseConfiguration(uri.fsPath, content, session);

        if (!amf.isFormatValid) {
          for (const err of amf.formatErrors) {
            repo.session.channels.warning(`Parse errors in metadata file ${err}}`);
          }
          throw new Error('invalid format');
        }

        let anyErrors = false;
        for (const err of amf.validate()) {
          repo.session.channels.warning(amf.formatVMessage(err));
          anyErrors = true;
        }

        if (anyErrors) {
          throw new Error('invalid manifest');
        }

        let fileUpdated = false;
        for (const warning of amf.deprecationWarnings()) {
          if (normalize) {
            amf.normalize();
            fileUpdated = true;
          } else {
            repo.session.channels.warning(amf.formatVMessage(warning));
          }
        }

        repo.session.channels.debug(`Inserting ${uri.formatted} into index.`);
        repo.index.insert(amf, repo.cacheFolder.relative(uri));

        if (fileUpdated) {
          await amf.save(uri);
        }
      } catch (e: any) {
        repo.session.channels.debug(e.toString());
        repo.session.channels.warning(`skipping invalid metadata file ${uri.fsPath}`);
      }
    }

    async function process(folder: Uri) {
      for (const [entry, type] of await folder.readDirectory()) {
        if (type & FileType.Directory) {
          await process(entry);
          continue;
        }

        if (type & FileType.File && isYAML(entry.path)) {
          void q.enqueue(() => processFile(entry));
        }
      }
    }

    // process the files in the local folder
    await process(this.cacheFolder);
    await q.done;

    // we're done inserting values
    this.index.doneInsertion();

    this.loaded = true;
  }

  async search(parent: Registries, criteria?: SearchCriteria): Promise<Array<[Registry, string, Array<Artifact>]>> {
    await this.load();
    const query = this.index.where;

    if (criteria?.idOrShortName) {
      query.id.nameOrShortNameIs(criteria.idOrShortName);
    }

    if (criteria?.keyword) {
      query.id.contains(criteria.keyword);
    }

    if (criteria?.version) {
      query.version.rangeMatch(criteria.version);
    }

    return [...(await this.openArtifacts(query.items, parent)).entries()].map(each => [this, ...each]);
  }


  private async openArtifact(manifestPath: string, parent: Registries): Promise<Artifact> {
    const metadata = await MetadataFile.parseMetadata(this.cacheFolder.join(manifestPath), this.session, this);
    const id = metadata.id;
    return new Artifact(this.session,
      metadata,
      this.index.indexSchema.id.getShortNameOf(id) || id,
      this.installationFolder.join(id.replace(/[^\w]+/g, '.'), metadata.version),
      parent.getRegistryName(this),
      this.location
    ).init(this.session);
  }

  private async openArtifacts(manifestPaths: Array<string>, parent: Registries) {
    let metadataFiles = new Array<Artifact>();

    // load them up async, but throttled via a queue
    await manifestPaths.forEachAsync(async (manifest) => metadataFiles.push(await this.openArtifact(manifest, parent))).done;

    // sort the contents by version before grouping. (descending version)
    metadataFiles = metadataFiles.sort((a, b) => compare(b.metadata.version, a.metadata.version));

    // return a map.
    return metadataFiles.groupByMap(m => m.metadata.id, artifact => artifact);
  }

  async save(): Promise<void> {
    await this.indexYaml.writeFile(Buffer.from(`${THIS_IS_NOT_A_MANIFEST_ITS_AN_INDEX_STRING}\n${serialize(this.index.serialize()).replace(/\s*(\d*,)\n/g, '$1')}`));
  }

}
