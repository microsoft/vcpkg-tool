// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { MetadataFile } from './amf/metadata-file';
import { Activation, deactivate } from './artifacts/activation';
import { Artifact, InstalledArtifact } from './artifacts/artifact';
import { Registry } from './artifacts/registry';
import { configurationName, defaultConfig, globalConfigurationFile, postscriptVariable, registryIndexFile, undo } from './constants';
import { FileSystem, FileType } from './fs/filesystem';
import { HttpsFileSystem } from './fs/http-filesystem';
import { LocalFileSystem } from './fs/local-filesystem';
import { schemeOf, UnifiedFileSystem } from './fs/unified-filesystem';
import { VsixLocalFilesystem } from './fs/vsix-local-filesystem';
import { i } from './i18n';
import { installGit } from './installers/git';
import { installNuGet } from './installers/nuget';
import { installUnTar } from './installers/untar';
import { installUnZip } from './installers/unzip';
import { InstallEvents, InstallOptions } from './interfaces/events';
import { Installer } from './interfaces/metadata/installers/Installer';
import { AggregateRegistry } from './registries/aggregate-registry';
import { LocalRegistry } from './registries/LocalRegistry';
import { Registries } from './registries/registries';
import { RemoteRegistry } from './registries/RemoteRegistry';
import { isIndexFile, isMetadataFile } from './registries/standard-registry';
import { Channels, Stopwatch } from './util/channels';
import { Queue } from './util/promise';
import { isFilePath, Uri } from './util/uri';

/** The definition for an installer tool function */
type InstallerTool<T extends Installer = any> = (
  session: Session,
  name: string,
  version: string,
  targetLocation: Uri,
  install: T,
  events: Partial<InstallEvents>,
  options: Partial<InstallOptions>
) => Promise<void>


export type Context = { [key: string]: Array<string> | undefined; } & {
  readonly os: string;
  readonly arch: string;
  readonly windows: boolean;
  readonly osx: boolean;
  readonly linux: boolean;
  readonly freebsd: boolean;
  readonly x64: boolean;
  readonly x86: boolean;
  readonly arm: boolean;
  readonly arm64: boolean;
}

export type SessionSettings = {
  readonly vcpkgCommand?: string;
  readonly homeFolder?: string;
  readonly vcpkgArtifactsRoot?: string;
  readonly vcpkgDownloads?: string;
  readonly vcpkgRegistriesCache?: string;
  readonly telemetryEnabled: boolean;
}

/**
 * The Session class is used to hold a reference to the
 * message channels,
 * the filesystems,
 * and any other 'global' data that should be kept.
 *
 */
export class Session {
  /** @internal */
  readonly stopwatch = new Stopwatch();
  readonly fileSystem: FileSystem;
  readonly channels: Channels;
  readonly homeFolder: Uri;
  readonly tmpFolder: Uri;
  readonly installFolder: Uri;
  readonly registryFolder: Uri;
  readonly activation: Activation = new Activation(this);
  get vcpkgCommand() { return this.settings.vcpkgCommand; }

  readonly globalConfig: Uri;
  readonly downloads: Uri;
  readonly telemetryEnabled: boolean;
  currentDirectory: Uri;
  configuration!: MetadataFile;

  /** register installer functions here */
  private installers = new Map<string, InstallerTool>([
    ['nuget', installNuGet],
    ['unzip', installUnZip],
    ['untar', installUnTar],
    ['git', installGit]
  ]);

  readonly defaultRegistry: AggregateRegistry;
  private readonly registries = new Registries(this);

  processVcpkgArg(argSetting: string | undefined, defaultName: string): Uri {
    return argSetting ? this.fileSystem.file(argSetting) : this.homeFolder.join(defaultName);
  }

  constructor(currentDirectory: string, public readonly context: Context, public readonly settings: SessionSettings, public readonly environment: NodeJS.ProcessEnv) {
    this.fileSystem = new UnifiedFileSystem(this).
      register('file', new LocalFileSystem(this)).
      register('vsix', new VsixLocalFilesystem(this)).
      register(['https'], new HttpsFileSystem(this)
      );

    this.channels = new Channels(this);

    this.telemetryEnabled = this.settings['telemetryEnabled'];

    this.setupLogging();

    this.homeFolder = this.fileSystem.file(settings.homeFolder!);
    this.downloads = this.processVcpkgArg(settings.vcpkgDownloads, 'downloads');
    this.globalConfig = this.homeFolder.join(globalConfigurationFile);

    this.tmpFolder = this.homeFolder.join('tmp');

    this.registryFolder = this.processVcpkgArg(settings.vcpkgRegistriesCache, 'registries').join('artifact');
    this.installFolder = this.processVcpkgArg(settings.vcpkgArtifactsRoot, 'artifacts');

    this.currentDirectory = this.fileSystem.file(currentDirectory);

    // add built in registries
    this.defaultRegistry = new AggregateRegistry(this);
  }

  parseUri(uriOrPath: string | Uri): Uri {
    return (typeof uriOrPath === 'string') ? isFilePath(uriOrPath) ? this.fileSystem.file(uriOrPath) : this.fileSystem.parse(uriOrPath) : uriOrPath;
  }

  async parseLocation(location?: string): Promise<Uri | undefined> {
    if (location) {
      const scheme = schemeOf(location);
      // file uri or drive letter
      if (scheme) {
        if (scheme.toLowerCase() !== 'file' && scheme.length !== 1) {
          // anything else with a colon isn't a legal path in any way.
          return undefined;
        }
        // must be a file path of some kind.
        const uri = this.parseUri(location);
        return await uri.exists() ? uri : undefined;
      }

      // is it an absolute path?
      if (location.startsWith('/') || location.startsWith('\\')) {
        const uri = this.fileSystem.file(location);
        return await uri.exists() ? uri : undefined;
      }

      // is it a path relative to the current directory?
      const uri = this.currentDirectory.join(location);
      return await uri.exists() ? uri : undefined;
    }
    return undefined;
  }

  async loadRegistry(registryLocation: Uri | string | undefined, registryKind = 'artifact'): Promise<Registry | undefined> {
    // normalize the location first.

    registryLocation = typeof registryLocation === 'string' ? await this.parseLocation(registryLocation) || this.parseUri(registryLocation) : registryLocation;

    if (!registryLocation) {
      return undefined;
    }

    // if the registry from that location is already loaded, return it.
    const r = this.registries.getRegistry(registryLocation.toString());
    if (r) {
      return r;
    }

    // not already loaded
    registryLocation = this.parseUri(registryLocation);

    switch (registryKind) {

      case 'artifact':
        switch (registryLocation.scheme) {
          case 'https':
            return this.registries.add(new RemoteRegistry(this, registryLocation));

          case 'file':
            return this.registries.add(new LocalRegistry(this, registryLocation));

          default:
            throw new Error(i`Unsupported registry scheme '${registryLocation.scheme}'`);
        }
    }
    throw new Error(i`Unsupported registry kind '${registryKind}'`);
  }

  async isLocalRegistry(location: Uri | string): Promise<boolean> {
    location = this.parseUri(location);

    if (location.scheme === 'file') {
      if (await isIndexFile(location)) {
        return true;
      }

      if (await location.isDirectory()) {
        const index = location.join(registryIndexFile);
        if (await isIndexFile(index)) {
          return true;
        }
        const s = this;
        let result = false;
        const q = new Queue();

        // still could be a folder of artifact files
        // eslint-disable-next-line no-inner-declarations
        async function process(folder: Uri) {
          for (const [entry, type] of await folder.readDirectory()) {
            if (result) {
              return;
            }

            if (type & FileType.Directory) {
              await process(entry);
              continue;
            }

            if (type & FileType.File && entry.path.endsWith('.json')) {
              void q.enqueue(async () => { result = result || await isMetadataFile(entry, s); });
            }
          }
        }
        await process(location);
        await q.done;
        return result; // whatever we guess, we'll use
      }
      return false;
    }

    return false;
  }

  async isRemoteRegistry(location: Uri | string): Promise<boolean> {
    return this.parseUri(location).scheme === 'https';
  }

  parseName(id: string): [string, string] {
    const parts = id.split(':');
    switch (parts.length) {
      case 0:
        throw new Error(i`Invalid artifact id '${id}'`);
      case 1:
        return ['default', id];
    }
    return <[string, string]>parts;
  }

  get vcpkgInstalled(): Promise<boolean> {
    return this.homeFolder.exists('.vcpkg-root');
  }

  async saveConfig() {
    await this.configuration.save(this.globalConfig);
  }

  #postscriptFile?: Uri;
  get postscriptFile() {
    return this.#postscriptFile || (this.#postscriptFile = this.environment[postscriptVariable] ? this.fileSystem.file(this.environment[postscriptVariable]!) : undefined);
  }

  async init() {
    // load global configuration
    if (!await this.fileSystem.isDirectory(this.homeFolder)) {
      // let's create the folder
      try {
        await this.fileSystem.createDirectory(this.homeFolder);
      } catch (error: any) {
        // if this throws, let it
        this.channels.debug(error?.message);
      }
      // check if it got made, because at an absolute minimum, we need a folder, so failing this is catastrophic.
      strict.ok(await this.fileSystem.isDirectory(this.homeFolder), i`Fatal: The root folder '${this.homeFolder.fsPath}' can not be created`);
    }

    if (!await this.fileSystem.isFile(this.globalConfig)) {
      try {
        await this.globalConfig.writeUTF8(defaultConfig);
      } catch {
        // if this throws, let it
      }
      // check if it got made, because at an absolute minimum, we need the config file, so failing this is catastrophic.
      strict.ok(await this.fileSystem.isFile(this.globalConfig), i`Fatal: The global configuration file '${this.globalConfig.fsPath}' can not be created`);
    }

    // got past the checks, let's load the configuration.
    this.configuration = await MetadataFile.parseMetadata(this.globalConfig, this);
    this.channels.debug(`Loaded global configuration file '${this.globalConfig.fsPath}'`);

    // load the registries
    for (const [name, regDef] of this.configuration.registries) {
      const loc = regDef.location.get(0);
      if (loc) {
        const uri = this.parseUri(loc);
        const reg = await this.loadRegistry(uri, regDef.registryKind);
        if (reg) {
          this.channels.debug(`Loaded global manifest ${name} => ${uri.formatted}`);
          this.defaultRegistry.add(reg, name);
        }
      }
    }

    return this;
  }

  async findProjectProfile(startLocation = this.currentDirectory, search = true): Promise<Uri | undefined> {
    let location = startLocation;
    const path = location.join(configurationName);
    if (await this.fileSystem.isFile(path)) {
      return path;
    }
    location = location.join('..');
    if (search) {
      return (location.toString() === startLocation.toString()) ? undefined : this.findProjectProfile(location);
    }
    return undefined;
  }

  async deactivate() {
    const previous = this.environment[undo];
    if (previous && this.postscriptFile) {
      const deactivationDataFile = this.parseUri(previous);
      if (deactivationDataFile.scheme === 'file' && await deactivationDataFile.exists()) {

        const deactivatationData = JSON.parse(await deactivationDataFile.readUTF8());
        delete deactivatationData.environment[undo];
        await deactivate(this.postscriptFile, deactivatationData.environment || {}, deactivatationData.aliases || {});
        await deactivationDataFile.delete();
      }
    }
  }


  setupLogging() {
    // at this point, we can subscribe to the events in the export * from './lib/version';FileSystem and Channels
    // and do what we need to do (record, store, etc.)
    //
    // (We'll defer actually this until we get to #23: Create Bug Report)
    //
    // this.FileSystem.on('deleted', (uri) => { console.debug(uri) })
  }

  async getInstalledArtifacts() {
    const result = new Array<{ folder: Uri, id: string, artifact: Artifact }>();
    if (! await this.installFolder.exists()) {
      return result;
    }
    for (const [folder, stat] of await this.installFolder.readDirectory(undefined, { recursive: true })) {
      try {
        const metadata = await MetadataFile.parseMetadata(folder.join('artifact.json'), this);
        result.push({
          folder,
          id: metadata.id,
          artifact: await new InstalledArtifact(this, metadata).init(this)
        });
      } catch {
        // not a valid install.
      }
    }
    return result;
  }

  /** returns an installer function (or undefined) for a given installerkind */
  artifactInstaller(installInfo: Installer) {
    return this.installers.get(installInfo.installerKind);
  }

  async openManifest(manifestFile: Uri): Promise<MetadataFile> {
    return await MetadataFile.parseConfiguration(manifestFile.fsPath, await manifestFile.readUTF8(), this);
  }

  serializer(key: any, value: any) {
    if (value instanceof Map) {
      return { dataType: 'Map', value: Array.from(value.entries()) };
    }
    return value;
  }

  deserializer(key: any, value: any) {
    if (typeof value === 'object' && value !== null) {
      switch (value.dataType) {
        case 'Map':
          return new Map(value.value);
      }
      if (value.scheme && value.path) {
        return this.fileSystem.from(value);
      }
    }
    return value;
  }
}
