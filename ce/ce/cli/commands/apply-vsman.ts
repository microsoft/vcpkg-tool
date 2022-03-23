// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MetadataFile } from '../../amf/metadata-file';
import { acquireArtifactFile } from '../../fs/acquire';
import { FileType } from '../../fs/filesystem';
import { i } from '../../i18n';
import { session } from '../../main';
import { Session } from '../../session';
import { Uri } from '../../util/uri';
import { templateAmfApplyVsManifestInformation } from '../../willow/template-amf';
import { parseVsManFromChannel, VsManDatabase } from '../../willow/willow';
import { Command } from '../command';
import { log } from '../styling';
import { Switch } from '../switch';

class ChannelUri extends Switch {
  readonly switch = 'channel';

  get help() {
    return [
      i`The URI to the Visual Studio channel to apply.`
    ];
  }
}

class RepoRoot extends Switch {
  readonly switch = 'repo';

  get help() {
    return [
      i`The directory path to the root of the repo into which artifact metadata is to be generated.`
    ];
  }
}

export class ApplyVsManCommand extends Command {
  readonly command = 'z-apply-vsman';
  readonly seeAlso = [];
  readonly argumentsHelp = [];
  readonly aliases = [];

  readonly channelUri = new ChannelUri(this);
  readonly repoRoot = new RepoRoot(this);

  get summary() {
    return i`Apply Visual Studio Channel (.vsman) information to a prototypical artifact metadata.`;
  }

  get description() {
    return [
      i`This is used to mint artifacts metadata exactly corresponding to a release state in a Visual Studio channel.`,
    ];
  }

  /**
   * Process an input file.
   */
  static async processFile(session: Session, inputUri: Uri, repoRoot: Uri, vsManLookup: VsManDatabase) {
    const inputPath = inputUri.fsPath;
    session.channels.debug(i`Processing ${inputPath}...`);
    const inputContent = await inputUri.readUTF8();
    const outputContent = templateAmfApplyVsManifestInformation(session, inputPath, inputContent, vsManLookup);
    if (!outputContent) {
      session.channels.warning(i`Skipped processing ${inputPath}`);
      return 0;
    }

    const outputAmf = await MetadataFile.parseConfiguration(inputPath, outputContent, session);
    if (!outputAmf.isValid) {
      const errors = outputAmf.validationErrors.join('\n');
      session.channels.warning(i`After transformation, ${inputPath} did not result in a valid AMF; skipping:\n${outputContent}\n${errors}`);
      return 0;
    }

    const outputId = outputAmf.info.id;
    const outputIdLast = outputId.slice(outputId.lastIndexOf('/'));
    const outputVersion = outputAmf.info.version;
    const outputRelativePath = `${outputId}/${outputIdLast}-${outputVersion}.yaml`;
    const outputFullPath = repoRoot.join(outputRelativePath);
    let doWrite = true;
    try {
      const outputExistingContent = await outputFullPath.readUTF8();
      if (outputExistingContent === outputContent) {
        doWrite = false;
      } else {
        session.channels.warning(i`After transformation, ${inputPath} results in ${outputFullPath.toString()} which already exists; overwriting.`);
      }
    } catch {
      // nothing to do
    }

    if (doWrite) {
      await outputFullPath.writeUTF8(outputContent);
    }

    session.channels.debug(i`-> ${outputFullPath.toString()}`);
    return 1;
  }

  /**
   * Process an input file or directory, recursively.
   */
  static async processInput(session: Session, inputDirectoryEntry: [Uri, FileType], repoRoot: Uri, vsManLookup: VsManDatabase): Promise<number> {
    if ((inputDirectoryEntry[1] & FileType.Directory) !== 0) {
      let total = 0;
      for (const child of await inputDirectoryEntry[0].readDirectory()) {
        total += await ApplyVsManCommand.processInput(session, child, repoRoot, vsManLookup);
      }

      return total;
    } else if ((inputDirectoryEntry[1] & FileType.File) !== 0) {
      return await ApplyVsManCommand.processFile(session, inputDirectoryEntry[0], repoRoot, vsManLookup);
    }

    return 0;
  }

  override async run() {
    const channelUriStr = this.channelUri.requiredValue;
    const repoRoot = session.fileSystem.file(this.repoRoot.requiredValue);
    log(i`Downloading channel manifest from ${channelUriStr}`);
    const channelUriUri = session.parseUri(channelUriStr);
    const channelFile = await acquireArtifactFile(session, [channelUriUri], 'channel.chman', {});
    const vsManPayload = parseVsManFromChannel(await channelFile.readUTF8());
    log(i`Downloading Visual Studio manifest version ${vsManPayload.version} (${vsManPayload.url})`);
    const vsManUri = await acquireArtifactFile(session, [session.parseUri(vsManPayload.url)], vsManPayload.fileName, {});
    const vsManLookup = new VsManDatabase(await vsManUri.readUTF8());
    let totalProcessed = 0;
    for (const inputPath of this.inputs) {
      const inputUri = session.fileSystem.file(inputPath);
      const inputStat = await inputUri.stat();
      totalProcessed += await ApplyVsManCommand.processInput(session, [inputUri, inputStat.type], repoRoot, vsManLookup);
    }

    session.channels.message(i`Processed ${totalProcessed} templates.`);
    return true;
  }
}
