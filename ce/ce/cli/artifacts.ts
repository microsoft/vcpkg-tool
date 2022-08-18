// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MultiBar, SingleBar } from 'cli-progress';
import { Artifact, ArtifactMap } from '../artifacts/artifact';
import { i } from '../i18n';
import { trackAcquire } from '../insights';
import { Registries } from '../registries/registries';
import { Session } from '../session';
import { artifactIdentity, artifactReference } from './format';
import { Table } from './markdown-table';
import { debug, error, log } from './styling';

export async function showArtifacts(artifacts: Iterable<Artifact>, options?: { force?: boolean }) {
  let failing = false;
  const table = new Table(i`Artifact`, i`Version`, i`Status`, i`Dependency`, i`Summary`);
  for (const artifact of artifacts) {
    const name = artifactIdentity(artifact.registryId, artifact.id, artifact.shortName);
    for (const err of artifact.metadata.validate()) {
      failing = true;
      error(artifact.metadata.formatVMessage(err));
    }
    table.push(name, artifact.version, options?.force || await artifact.isInstalled ? 'installed' : 'will install', artifact.isPrimary ? ' ' : '*', artifact.metadata.summary || '');
  }
  log(table.toString());
  log();

  return !failing;
}

export type Selections = Map<string, string>;

export async function selectArtifacts(selections: Selections, registries: Registries): Promise<false | ArtifactMap> {
  const artifacts = new ArtifactMap();

  for (const [identity, version] of selections) {
    const [registry, id, artifact] = await registries.getArtifact(identity, version) || [];

    if (!artifact) {
      error(`Unable to resolve artifact: ${artifactReference('', identity, version)}`);

      const results = await registries.search({ keyword: identity, version: version });
      if (results.length) {
        log('\nPossible matches:');
        for (const [reg, key, arts] of results) {
          log(`  ${artifactReference(registries.getRegistryName(reg), key, '')}`);
        }
      }

      return false;
    }

    artifacts.set(artifact.uniqueId, [artifact, identity, version]);
    artifact.isPrimary = true;
    await artifact.resolveDependencies(artifacts);
  }
  return artifacts;
}

enum TaggedProgressKind {
  Unset,
  Verifying,
  Downloading,
  GenericProgress,
  Heartbeat
}

class TaggedProgressBar {
  private bar: SingleBar | undefined;
  private kind = TaggedProgressKind.Unset;
  public lastCurrentValue = 0;
  constructor(private readonly multiBar: MultiBar) {
  }

  private checkChangeKind(currentValue: number, kind: TaggedProgressKind) {
    this.lastCurrentValue = currentValue;
    if (this.kind !== kind) {
      if (this.bar) {
        this.multiBar.remove(this.bar);
        this.bar = undefined;
      }

      this.kind = kind;
    }
  }

  startOrUpdate(kind: TaggedProgressKind, total: number, currentValue: number, suffix: string) {
    this.checkChangeKind(currentValue, kind);
    const payload = { suffix: suffix };
    if (this.bar) {
      this.bar.update(currentValue, payload);
    } else {
      this.kind = kind;
      this.bar = this.multiBar.create(total, currentValue, payload, { format: '{bar}\u25A0 {percentage}% {suffix}' });
    }
  }

  heartbeat(suffix: string) {
    this.checkChangeKind(0, TaggedProgressKind.Heartbeat);
    const payload = { suffix: suffix };
    if (this.bar) {
      this.bar.update(0, payload);
    } else {
      const progressUnknown = i`(progress unknown)`;
      const totalSpaces = 41 - progressUnknown.length;
      const prefixSpaces = Math.floor(totalSpaces / 2);
      const suffixSpaces = totalSpaces - prefixSpaces;
      const prettyProgressUnknown = Array(prefixSpaces).join(' ') + progressUnknown + Array(suffixSpaces).join(' ');
      this.bar = this.multiBar.create(0, 0, payload, { format: '\u25A0' + prettyProgressUnknown + '\u25A0 {suffix}' });
    }
  }
}

export async function installArtifacts(session: Session, artifacts: Array<Artifact>, options?: { force?: boolean, allLanguages?: boolean, language?: string }): Promise<[boolean, Map<Artifact, boolean>]> {
  // resolve the full set of artifacts to install.
  const installed = new Map<Artifact, boolean>();
  const bar = new MultiBar({
    clearOnComplete: true, hideCursor: true,
    barCompleteChar: '\u25A0',
    barIncompleteChar: ' ',
    etaBuffer: 40
  });

  const overallProgress = bar.create(artifacts.length, 0, { name: '' }, { format: '{bar}\u25A0 [{value}/{total}] {name}', emptyOnZero: true });
  const individualProgress = new TaggedProgressBar(bar);

  const spinnerValue = 0;

  for (let idx = 0; idx < artifacts.length; ++idx) {
    const artifact = artifacts[idx];
    const id = artifact.id;
    const registryName = artifact.registryId;
    overallProgress.update(idx, { name: artifactIdentity(registryName, id) });
    try {
      const actuallyInstalled = await artifact.install({
        verifying: (current, percent) => {
          individualProgress.startOrUpdate(TaggedProgressKind.Verifying, 100, percent, i`verifying` + ' ' + current);
        },
        download: (current, percent) => {
          individualProgress.startOrUpdate(TaggedProgressKind.Downloading, 100, percent, i`downloading` + ' ' + current);
        },
        fileProgress: (entry) => {
          let suffix = entry.extractPath;
          if (suffix) {
            suffix = ' ' + suffix;
          } else {
            suffix = '';
          }

          individualProgress.startOrUpdate(TaggedProgressKind.GenericProgress, 100, individualProgress.lastCurrentValue, i`unpacking` + suffix);
        },
        progress: (percent: number) => {
          individualProgress.startOrUpdate(TaggedProgressKind.GenericProgress, 100, percent, i`unpacking`);
        },
        heartbeat: (text: string) => {
          individualProgress.heartbeat(text);
        }
      }, options || {});
      // remember what was actually installed
      installed.set(artifact, actuallyInstalled);
      if (actuallyInstalled) {
        trackAcquire(id, artifact.version);
      }
    } catch (e: any) {
      bar.stop();
      debug(e);
      debug(e.stack);
      error(i`Error installing ${artifactIdentity(registryName, id)} - ${e} `);
      return [false, installed];
    }
  }

  bar.stop();
  return [true, installed];
}
