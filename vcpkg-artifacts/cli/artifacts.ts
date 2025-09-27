// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MultiBar, SingleBar } from 'cli-progress';
import { Artifact, ArtifactBase, InstallStatus, ResolvedArtifact, Selections, resolveDependencies } from '../artifacts/artifact';
import { i } from '../i18n';
import { InstallEvents } from '../interfaces/events';
import { RegistryDisplayContext, RegistryResolver, getArtifact } from '../registries/registries';
import { Session } from '../session';
import { Channels } from '../util/channels';
import { Uri } from '../util/uri';
import { Table } from './console-table';
import { addVersionToArtifactIdentity, artifactIdentity } from './format';
import { debug, error, log } from './styling';

export async function showArtifacts(artifacts: Iterable<ResolvedArtifact>, registries: RegistryDisplayContext, options?: { force?: boolean }) {
  let failing = false;
  const table = new Table(i`Artifact`, i`Version`, i`Status`, i`Dependency`, i`Summary`);
  for (const resolved of artifacts) {
    const artifact = resolved.artifact;
    if (artifact instanceof Artifact) {
      const name = artifactIdentity(registries.getRegistryDisplayName(artifact.registryUri), artifact.id, artifact.shortName);
      for (const err of artifact.metadata.validate()) {
        failing = true;
        error(artifact.metadata.formatVMessage(err));
      }
      table.push(name, artifact.version, options?.force || await artifact.isInstalled ? 'installed' : 'will install', resolved.initialSelection ? ' ' : '*', artifact.metadata.summary || '');
    }
  }

  log(table.toString());
  log();
  return !failing;
}

export interface SelectedArtifact extends ResolvedArtifact {
  requestedVersion: string | undefined;
}

export async function selectArtifacts(session: Session, selections: Selections, registries: RegistryResolver, dependencyDepth: number): Promise<false | Array<SelectedArtifact>> {
  const userSelectedArtifacts = new Map<string, ArtifactBase>();
  const userSelectedVersions = new Map<string, string>();
  for (const [idOrShortName, version] of selections) {
    const [, artifact] = await getArtifact(registries, idOrShortName, version) || [];

    if (!artifact) {
      error(`Unable to resolve artifact: ${addVersionToArtifactIdentity(idOrShortName, version)}`);

      const results = await registries.search({ keyword: idOrShortName, version: version });
      if (results.length) {
        log('Possible matches:');
        for (const [artifactDisplay, artifactVersions] of results) {
          for (const artifactVersion of artifactVersions) {
            log(`  ${addVersionToArtifactIdentity(artifactDisplay, artifactVersion.version)}`);
          }
        }
      }

      return false;
    }

    userSelectedArtifacts.set(artifact.uniqueId, artifact);
    userSelectedVersions.set(artifact.uniqueId, version);
  }

  const allResolved = await resolveDependencies(session, registries, Array.from(userSelectedArtifacts.values()), dependencyDepth);
  const results = new Array<SelectedArtifact>();
  for (const resolved of allResolved) {
    results.push({...resolved, 'requestedVersion': userSelectedVersions.get(resolved.uniqueId)});
  }

  return results;
}

interface ProgressRenderer extends InstallEvents {
  setArtifactIndex(index: number, displayName: string): void;
  stop(): void;
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
      this.bar = this.multiBar.create(total, currentValue, payload, { format: '{bar} {percentage}% {suffix}' });
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
      this.bar = this.multiBar.create(0, 0, payload, { format: '*' + prettyProgressUnknown + '* {suffix}' });
    }
  }
}

class TtyProgressRenderer implements Partial<ProgressRenderer> {
  readonly #bar = new MultiBar({
    clearOnComplete: true,
    hideCursor: true,
    barCompleteChar: '*',
    barIncompleteChar: ' ',
    etaBuffer: 40
  });
  readonly #overallProgress : SingleBar;
  readonly #individualProgress : TaggedProgressBar;

  constructor(totalArtifactCount: number) {
    this.#overallProgress = this.#bar.create(totalArtifactCount, 0, { name: '' }, { format: `{bar} [{value}/${totalArtifactCount - 1}] {name}`, emptyOnZero: true });
    this.#individualProgress = new TaggedProgressBar(this.#bar);
  }

  setArtifactIndex(index: number, displayName: string): void {
    this.#overallProgress.update(index, { name: displayName });
  }

  hashVerifyProgress(file: string, percent: number) {
    this.#individualProgress.startOrUpdate(TaggedProgressKind.Verifying, 100, percent, i`verifying` + ' ' + file);
  }

  downloadProgress(uri: Uri, destination: string, percent: number) {
    this.#individualProgress.startOrUpdate(TaggedProgressKind.Downloading, 100, percent, i`downloading ${uri.toString()} -> ${destination}`);
  }

  unpackArchiveStart(archiveUri: Uri) {
    this.#individualProgress.heartbeat(i`unpacking ${archiveUri.fsPath}`);
  }

  unpackArchiveHeartbeat(text: string) {
    this.#individualProgress.heartbeat(text);
  }

  stop() {
    this.#bar.stop();
  }
}

const downloadUpdateRateMs = 10 * 1000;

class NoTtyProgressRenderer implements Partial<ProgressRenderer> {
  #currentIndex = 0;
  #downloadPrecent = 0;
  #downloadTimeoutId: NodeJS.Timeout | undefined;
  constructor(private readonly channels: Channels, private readonly totalArtifactCount: number) {}

  setArtifactIndex(index: number): void {
    this.#currentIndex = index;
  }

  startInstallArtifact(displayName: string) {
    this.channels.message(`[${this.#currentIndex + 1}/${this.totalArtifactCount - 1}] ` + i`Installing ${displayName}...`);
  }

  alreadyInstalledArtifact(displayName: string) {
    this.channels.message(`[${this.#currentIndex + 1}/${this.totalArtifactCount - 1}] ` + i`${displayName} already installed.`);
  }

  downloadStart(uris: Array<Uri>, _destination: string) {
    let displayUri: string;
    if (uris.length === 1) {
      displayUri = uris[0].toString();
    } else {
      displayUri = JSON.stringify(uris.map(uri => uri.toString()));
    }

    this.channels.message(i`Downloading ${displayUri}...`);
    this.#downloadTimeoutId = setTimeout(this.downloadProgressDisplay.bind(this), downloadUpdateRateMs);
  }

  downloadProgress(_uri: Uri, _destination: string, percent: number): void {
    this.#downloadPrecent = percent;
  }

  downloadProgressDisplay() {
    this.channels.message(`${this.#downloadPrecent}%`);
    this.#downloadTimeoutId = setTimeout(this.downloadProgressDisplay.bind(this), downloadUpdateRateMs);
  }

  downloadComplete(): void {
    if (this.#downloadTimeoutId) {
      clearTimeout(this.#downloadTimeoutId);
    }
  }

  stop(): void {
    if (this.#downloadTimeoutId) {
      clearTimeout(this.#downloadTimeoutId);
    }
  }

  unpackArchiveStart(archiveUri: Uri) {
    this.channels.message(i`Unpacking ${archiveUri.fsPath}...`);
  }
}

export async function acquireArtifacts(session: Session, resolved: Array<ResolvedArtifact>, registries: RegistryDisplayContext, options?: { force?: boolean, allLanguages?: boolean, language?: string }): Promise<boolean> {
  // resolve the full set of artifacts to install.
  const isTty = process.stdout.isTTY === true;
  const progressRenderer : Partial<ProgressRenderer> = isTty ? new TtyProgressRenderer(resolved.length) : new NoTtyProgressRenderer(session.channels, resolved.length);
  for (let idx = 0; idx < resolved.length; ++idx) {
    const artifact = resolved[idx].artifact;
    if (artifact instanceof Artifact) {
      const id = artifact.id;
      const registryName = registries.getRegistryDisplayName(artifact.registryUri);
      const artifactDisplayName = artifactIdentity(registryName, id, artifact.shortName);
      progressRenderer.setArtifactIndex?.(idx, artifactDisplayName);
      try {
        const installStatus = await artifact.install(artifactDisplayName, progressRenderer, options || {});
        switch (installStatus) {
          case InstallStatus.Installed:
            session.trackAcquire(artifact.registryUri.toString(), id, artifact.version);
            break;
          case InstallStatus.AlreadyInstalled:
            break;
          case InstallStatus.Failed:
            progressRenderer.stop?.();
            return false;
        }
      } catch (e: any) {
        progressRenderer.stop?.();
        debug(e);
        debug(e.stack);
        error(i`Error installing ${artifactDisplayName} - ${e}`);
        return false;
      }
    }
  }

  progressRenderer.stop?.();
  return true;
}
