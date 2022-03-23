// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/**
 * @license node-stream-zip | (c) 2020 Antelle | https://github.com/antelle/node-stream-zip/blob/master/LICENSE
 * Portions copyright https://github.com/cthackers/adm-zip | https://raw.githubusercontent.com/cthackers/adm-zip/master/LICENSE
 */

import { Readable, Transform, TransformCallback } from 'stream';
import { constants, createInflateRaw } from 'zlib';
import { ReadHandle } from '../fs/filesystem';

interface ZipParseState {
  window: FileWindowBuffer;
  totalReadLength: number;
  minPos: number;
  lastPos: any;
  chunkSize: any;
  firstByte: number;
  sig: number;
  lastBufferPosition?: number;
  pos: number;
  entry: ZipEntry | null;
  entriesLeft: number;
  move: boolean;
}

export class ZipFile {
  private centralDirectory!: CentralDirectoryHeader;
  private chunkSize = 0;
  private state!: ZipParseState;
  private fileSize = 0;

  /** file entries in the zip file */
  public files = new Map<string, ZipEntry>();

  /** folder entries in the zip file */
  public folders = new Map<string, ZipEntry>();

  /**
   * archive comment
   */
  public comment?: string;

  static async read(readHandle: ReadHandle) {
    const result = new ZipFile(readHandle);
    await result.readCentralDirectory();
    return result;
  }

  close() {
    return this.readHandle.close();
  }

  private constructor(private readHandle: ReadHandle) {
  }

  private async readUntilFound(): Promise<void> {
    let pos = this.state.lastPos;
    let bufferPosition = pos - this.state.window.position;
    const buffer = this.state.window.buffer;
    const minPos = this.state.minPos;
    while (--pos >= minPos && --bufferPosition >= 0) {
      if (buffer.length - bufferPosition >= 4 && buffer[bufferPosition] === this.state.firstByte) {
        // quick check first signature byte
        if (buffer.readUInt32LE(bufferPosition) === this.state.sig) {
          this.state.lastBufferPosition = bufferPosition;
          return;
        }
      }
    }
    if (pos === minPos) {
      throw new Error('Bad archive');
    }

    this.state.lastPos = pos + 1;
    this.state.chunkSize *= 2;
    if (pos <= minPos) {
      throw new Error('Bad archive');
    }

    const expandLength = Math.min(this.state.chunkSize, pos - minPos);
    await this.state.window.expandLeft(expandLength);
    return this.readUntilFound();
  }

  async readCentralDirectory() {
    this.fileSize = await this.readHandle.size();
    this.chunkSize = this.chunkSize || Math.round(this.fileSize / 1000);
    this.chunkSize = Math.max(
      Math.min(this.chunkSize, Math.min(128 * 1024, this.fileSize)),
      Math.min(1024, this.fileSize)
    );

    const totalReadLength = Math.min(consts.ENDHDR + consts.MAXFILECOMMENT, this.fileSize);
    this.state = <ZipParseState>{
      window: new FileWindowBuffer(this.readHandle),
      totalReadLength,
      minPos: this.fileSize - totalReadLength,
      lastPos: this.fileSize,
      chunkSize: Math.min(1024, this.chunkSize),
      firstByte: consts.ENDSIGFIRST,
      sig: consts.ENDSIG,
    };
    await this.state.window.read(this.fileSize - this.state.chunkSize, this.state.chunkSize);
    await this.readUntilFound();

    const buffer = this.state.window.buffer;
    const pos = this.state.lastBufferPosition || 0;

    this.centralDirectory = new CentralDirectoryHeader(buffer.slice(pos, pos + consts.ENDHDR));

    this.centralDirectory.headerOffset = this.state.window.position + pos;
    this.comment = this.centralDirectory.commentLength ? buffer.slice(pos + consts.ENDHDR, pos + consts.ENDHDR + this.centralDirectory.commentLength).toString() : undefined;

    if ((this.centralDirectory.volumeEntries === consts.EF_ZIP64_OR_16 && this.centralDirectory.totalEntries === consts.EF_ZIP64_OR_16) || this.centralDirectory.size === consts.EF_ZIP64_OR_32 || this.centralDirectory.offset === consts.EF_ZIP64_OR_32) {
      return this.readZip64CentralDirectoryLocator();
    } else {
      this.state = <ZipParseState>{};
      return this.readEntries();
    }
  }

  private async readZip64CentralDirectoryLocator() {
    const length = consts.ENDL64HDR;
    if (this.state.lastBufferPosition! > length) {
      this.state.lastBufferPosition! -= length;
    } else {
      this.state = <ZipParseState>{
        window: this.state.window,
        totalReadLength: length,
        minPos: this.state.window.position - length,
        lastPos: this.state.window.position,
        chunkSize: this.state.chunkSize,
        firstByte: consts.ENDL64SIGFIRST,
        sig: consts.ENDL64SIG,
      };
      await this.state.window.read(this.state.lastPos - this.state.chunkSize, this.state.chunkSize);
      await this.readUntilFound();
    }

    let buffer = this.state.window.buffer;
    const locHeader = new CentralDirectoryLoc64Header(
      buffer.slice(this.state.lastBufferPosition, this.state.lastBufferPosition! + consts.ENDL64HDR)
    );
    const readLength = this.fileSize - locHeader.headerOffset;
    this.state = <ZipParseState>{
      window: this.state.window,
      totalReadLength: readLength,
      minPos: locHeader.headerOffset,
      lastPos: this.state.lastPos,
      chunkSize: this.state.chunkSize,
      firstByte: consts.END64SIGFIRST,
      sig: consts.END64SIG,
    };
    await this.state.window.read(this.fileSize - this.state.chunkSize, this.state.chunkSize);
    await this.readUntilFound();

    buffer = this.state.window.buffer;
    const zip64cd = new CentralDirectoryZip64Header(buffer.slice(this.state.lastBufferPosition, this.state.lastBufferPosition! + consts.END64HDR));
    this.centralDirectory.volumeEntries = zip64cd.volumeEntries;
    this.centralDirectory.totalEntries = zip64cd.totalEntries;
    this.centralDirectory.size = zip64cd.size;
    this.centralDirectory.offset = zip64cd.offset;

    this.state = <ZipParseState>{};
    return this.readEntries();
  }

  private async readEntries() {
    this.state = <ZipParseState>{
      window: new FileWindowBuffer(this.readHandle),
      pos: this.centralDirectory.offset,
      chunkSize: this.chunkSize,
      entriesLeft: this.centralDirectory.volumeEntries,
    };

    await this.state.window.read(this.state.pos, Math.min(this.chunkSize, this.fileSize - this.state.pos));

    while (this.state.entriesLeft) {
      let bufferPos = this.state.pos - this.state.window.position;
      let entry = this.state.entry;
      const buffer = this.state.window.buffer;
      const bufferLength = buffer.length;

      if (!entry) {
        entry = new ZipEntry(this, buffer, bufferPos);
        entry.headerOffset = this.state.window.position + bufferPos;
        this.state.entry = entry;
        this.state.pos += consts.CENHDR;
        bufferPos += consts.CENHDR;
      }
      const entryHeaderSize = entry.fnameLen + entry.extraLen + entry.comLen;
      const advanceBytes = entryHeaderSize + (this.state.entriesLeft > 1 ? consts.CENHDR : 0);

      // if there isn't enough bytes read, read 'em.
      if (bufferLength - bufferPos < advanceBytes) {
        await this.state.window.moveRight(bufferPos);
        continue;
      }

      entry.processFilename(buffer, bufferPos);
      entry.validateName();
      if (entry.isDirectory) {
        this.folders.set(entry.name, entry);
      } else {
        this.files.set(entry.name, entry);
      }

      this.state.entry = entry = null;
      this.state.entriesLeft--;
      this.state.pos += entryHeaderSize;
      bufferPos += entryHeaderSize;
    }
  }

  private dataOffset(entry: ZipEntry) {
    return entry.offset + consts.LOCHDR + entry.fnameLen + entry.extraLen;
  }

  /** @internal */
  async openEntry(entry: ZipEntry) {
    // is this a file?
    if (!entry.isFile) {
      throw new Error(`Entry ${entry} not is not a file`);
    }

    // let's check to see if it's encrypted.
    const buffer = Buffer.alloc(consts.LOCHDR);
    await this.readHandle.readComplete(buffer, 0, buffer.length, entry.offset);

    entry.parseDataHeader(buffer);
    if (entry.encrypted) {
      throw new Error('Entry encrypted');
    }

    const offset = this.dataOffset((<ZipEntry>entry));
    let entryStream = this.readHandle.readStream(offset, offset + entry.compressedSize - 1);

    if (entry.method === consts.STORED) {
      // nothing to do
    } else if (entry.method === consts.DEFLATED) {
      entryStream = entryStream.pipe(createInflateRaw({ flush: constants.Z_SYNC_FLUSH }));
    } else {
      throw new Error('Unknown compression method: ' + entry.method);
    }

    // should check CRC?
    if ((entry.flags & 0x8) === 0x8) {
      entryStream = entryStream.pipe(createVerifier(entry.crc, entry.size));
    }

    return entryStream;
  }
}

const consts = {
  /* The local file header */
  LOCHDR: 30, // LOC header size
  LOCSIG: 0x04034b50, // "PK\003\004"
  LOCVER: 4, // version needed to extract
  LOCFLG: 6, // general purpose bit flag
  LOCHOW: 8, // compression method
  LOCTIM: 10, // modification time (2 bytes time, 2 bytes date)
  LOCCRC: 14, // uncompressed file crc-32 value
  LOCSIZ: 18, // compressed size
  LOCLEN: 22, // uncompressed size
  LOCNAM: 26, // filename length
  LOCEXT: 28, // extra field length

  /* The Data descriptor */
  EXTSIG: 0x08074b50, // "PK\007\008"
  EXTHDR: 16, // EXT header size
  EXTCRC: 4, // uncompressed file crc-32 value
  EXTSIZ: 8, // compressed size
  EXTLEN: 12, // uncompressed size

  /* The central directory file header */
  CENHDR: 46, // CEN header size
  CENSIG: 0x02014b50, // "PK\001\002"
  CENVEM: 4, // version made by
  CENVER: 6, // version needed to extract
  CENFLG: 8, // encrypt, decrypt flags
  CENHOW: 10, // compression method
  CENTIM: 12, // modification time (2 bytes time, 2 bytes date)
  CENCRC: 16, // uncompressed file crc-32 value
  CENSIZ: 20, // compressed size
  CENLEN: 24, // uncompressed size
  CENNAM: 28, // filename length
  CENEXT: 30, // extra field length
  CENCOM: 32, // file comment length
  CENDSK: 34, // volume number start
  CENATT: 36, // internal file attributes
  CENATX: 38, // external file attributes (host system dependent)
  CENOFF: 42, // LOC header offset

  /* The entries in the end of central directory */
  ENDHDR: 22, // END header size
  ENDSIG: 0x06054b50, // "PK\005\006"
  ENDSIGFIRST: 0x50,
  ENDSUB: 8, // number of entries on this disk
  ENDTOT: 10, // total number of entries
  ENDSIZ: 12, // central directory size in bytes
  ENDOFF: 16, // offset of first CEN header
  ENDCOM: 20, // zip file comment length
  MAXFILECOMMENT: 0xffff,

  /* The entries in the end of ZIP64 central directory locator */
  ENDL64HDR: 20, // ZIP64 end of central directory locator header size
  ENDL64SIG: 0x07064b50, // ZIP64 end of central directory locator signature
  ENDL64SIGFIRST: 0x50,
  ENDL64OFS: 8, // ZIP64 end of central directory offset

  /* The entries in the end of ZIP64 central directory */
  END64HDR: 56, // ZIP64 end of central directory header size
  END64SIG: 0x06064b50, // ZIP64 end of central directory signature
  END64SIGFIRST: 0x50,
  END64SUB: 24, // number of entries on this disk
  END64TOT: 32, // total number of entries
  END64SIZ: 40,
  END64OFF: 48,

  /* Compression methods */
  STORED: 0, // no compression
  SHRUNK: 1, // shrunk
  REDUCED1: 2, // reduced with compression factor 1
  REDUCED2: 3, // reduced with compression factor 2
  REDUCED3: 4, // reduced with compression factor 3
  REDUCED4: 5, // reduced with compression factor 4
  IMPLODED: 6, // imploded
  // 7 reserved
  DEFLATED: 8, // deflated
  ENHANCED_DEFLATED: 9, // deflate64
  PKWARE: 10, // PKWare DCL imploded
  // 11 reserved
  BZIP2: 12, //  compressed using BZIP2
  // 13 reserved
  LZMA: 14, // LZMA
  // 15-17 reserved
  IBM_TERSE: 18, // compressed using IBM TERSE
  IBM_LZ77: 19, //IBM LZ77 z

  /* General purpose bit flag */
  FLG_ENC: 0, // encrypted file
  FLG_COMP1: 1, // compression option
  FLG_COMP2: 2, // compression option
  FLG_DESC: 4, // data descriptor
  FLG_ENH: 8, // enhanced deflation
  FLG_STR: 16, // strong encryption
  FLG_LNG: 1024, // language encoding
  FLG_MSK: 4096, // mask header values
  FLG_ENTRY_ENC: 1,

  /* 4.5 Extensible data fields */
  EF_ID: 0,
  EF_SIZE: 2,

  /* Header IDs */
  ID_ZIP64: 0x0001,
  ID_AVINFO: 0x0007,
  ID_PFS: 0x0008,
  ID_OS2: 0x0009,
  ID_NTFS: 0x000a,
  ID_OPENVMS: 0x000c,
  ID_UNIX: 0x000d,
  ID_FORK: 0x000e,
  ID_PATCH: 0x000f,
  ID_X509_PKCS7: 0x0014,
  ID_X509_CERTID_F: 0x0015,
  ID_X509_CERTID_C: 0x0016,
  ID_STRONGENC: 0x0017,
  ID_RECORD_MGT: 0x0018,
  ID_X509_PKCS7_RL: 0x0019,
  ID_IBM1: 0x0065,
  ID_IBM2: 0x0066,
  ID_POSZIP: 0x4690,

  EF_ZIP64_OR_32: 0xffffffff,
  EF_ZIP64_OR_16: 0xffff,
};

class CentralDirectoryHeader {
  volumeEntries!: number;
  totalEntries!: number;
  size!: number;
  offset!: number;
  commentLength!: number;
  headerOffset!: number;

  constructor(data: Buffer) {
    if (data.length !== consts.ENDHDR || data.readUInt32LE(0) !== consts.ENDSIG) {
      throw new Error('Invalid central directory');
    }
    // number of entries on this volume
    this.volumeEntries = data.readUInt16LE(consts.ENDSUB);
    // total number of entries
    this.totalEntries = data.readUInt16LE(consts.ENDTOT);
    // central directory size in bytes
    this.size = data.readUInt32LE(consts.ENDSIZ);
    // offset of first CEN header
    this.offset = data.readUInt32LE(consts.ENDOFF);
    // zip file comment length
    this.commentLength = data.readUInt16LE(consts.ENDCOM);
  }
}

class CentralDirectoryLoc64Header {
  headerOffset!: number;

  constructor(data: Buffer) {
    if (data.length !== consts.ENDL64HDR || data.readUInt32LE(0) !== consts.ENDL64SIG) {
      throw new Error('Invalid zip64 central directory locator');
    }
    // ZIP64 EOCD header offset
    this.headerOffset = readUInt64LE(data, consts.ENDSUB);
  }
}

class CentralDirectoryZip64Header {
  volumeEntries!: number;
  totalEntries!: number;
  size!: number;
  offset!: number;

  constructor(data: Buffer) {
    if (data.length !== consts.END64HDR || data.readUInt32LE(0) !== consts.END64SIG) {
      throw new Error('Invalid central directory');
    }
    // number of entries on this volume
    this.volumeEntries = readUInt64LE(data, consts.END64SUB);
    // total number of entries
    this.totalEntries = readUInt64LE(data, consts.END64TOT);
    // central directory size in bytes
    this.size = readUInt64LE(data, consts.END64SIZ);
    // offset of first CEN header
    this.offset = readUInt64LE(data, consts.END64OFF);
  }
}

export class ZipEntry {
  /**
  * file name
  */
  name!: string;

  /**
   * true if it's a directory entry
   */
  isDirectory!: boolean;

  /**
   * file comment
   */
  comment!: string | null;

  /**
   * version made by
   */
  verMade: number;

  /**
   * version needed to extract
   */
  version: number;

  /**
   * encrypt, decrypt flags
   */
  flags: number;

  /**
   * compression method
   */
  method: number;

  /**
   * modification time
   */
  time: Date;

  /**
   * uncompressed file crc-32 value
   */
  crc: number;

  /**
   * compressed size
   */
  compressedSize: number;

  /**
   * uncompressed size
   */
  size: number;

  /**
   * volume number start
   */
  diskStart: number;

  /**
   * internal file attributes
   */
  inattr: number;

  /**
   * external file attributes
   */
  attr: number;

  /**
   * LOC header offset
   */
  offset: number;

  fnameLen: number;
  extraLen: number;
  comLen: number;
  headerOffset!: number;

  constructor(private zipFile: ZipFile, data: Buffer, offset: number) {
    // data should be 46 bytes and start with "PK 01 02"
    if (data.length < offset + consts.CENHDR || data.readUInt32LE(offset) !== consts.CENSIG) {
      throw new Error('Invalid entry header');
    }
    // version made by
    this.verMade = data.readUInt16LE(offset + consts.CENVEM);
    // version needed to extract
    this.version = data.readUInt16LE(offset + consts.CENVER);
    // encrypt, decrypt flags
    this.flags = data.readUInt16LE(offset + consts.CENFLG);
    // compression method
    this.method = data.readUInt16LE(offset + consts.CENHOW);
    // modification time (2 bytes time, 2 bytes date)
    const timebytes = data.readUInt16LE(offset + consts.CENTIM);
    const datebytes = data.readUInt16LE(offset + consts.CENTIM + 2);
    this.time = parseZipTime(timebytes, datebytes);

    // uncompressed file crc-32 value
    this.crc = data.readUInt32LE(offset + consts.CENCRC);
    // compressed size
    this.compressedSize = data.readUInt32LE(offset + consts.CENSIZ);
    // uncompressed size
    this.size = data.readUInt32LE(offset + consts.CENLEN);
    // filename length
    this.fnameLen = data.readUInt16LE(offset + consts.CENNAM);
    // extra field length
    this.extraLen = data.readUInt16LE(offset + consts.CENEXT);
    // file comment length
    this.comLen = data.readUInt16LE(offset + consts.CENCOM);
    // volume number start
    this.diskStart = data.readUInt16LE(offset + consts.CENDSK);
    // internal file attributes
    this.inattr = data.readUInt16LE(offset + consts.CENATT);
    // external file attributes
    this.attr = data.readUInt32LE(offset + consts.CENATX);
    // LOC header offset
    this.offset = data.readUInt32LE(offset + consts.CENOFF);
  }

  read(): Promise<Readable> {
    return this.zipFile.openEntry(this);
  }

  parseDataHeader(data: Buffer) {
    // 30 bytes and should start with "PK\003\004"
    if (data.readUInt32LE(0) !== consts.LOCSIG) {
      throw new Error('Invalid local header');
    }
    // version needed to extract
    this.version = data.readUInt16LE(consts.LOCVER);
    // general purpose bit flag
    this.flags = data.readUInt16LE(consts.LOCFLG);
    // compression method
    this.method = data.readUInt16LE(consts.LOCHOW);
    // modification time (2 bytes time ; 2 bytes date)
    const timebytes = data.readUInt16LE(consts.LOCTIM);
    const datebytes = data.readUInt16LE(consts.LOCTIM + 2);
    this.time = parseZipTime(timebytes, datebytes);

    // uncompressed file crc-32 value
    this.crc = data.readUInt32LE(consts.LOCCRC) || this.crc;
    // compressed size
    const compressedSize = data.readUInt32LE(consts.LOCSIZ);
    if (compressedSize && compressedSize !== consts.EF_ZIP64_OR_32) {
      this.compressedSize = compressedSize;
    }
    // uncompressed size
    const size = data.readUInt32LE(consts.LOCLEN);
    if (size && size !== consts.EF_ZIP64_OR_32) {
      this.size = size;
    }
    // filename length
    this.fnameLen = data.readUInt16LE(consts.LOCNAM);
    // extra field length
    this.extraLen = data.readUInt16LE(consts.LOCEXT);
  }

  processFilename(data: Buffer, offset: number) {
    this.name = data.slice(offset, (offset += this.fnameLen)).toString();
    const lastChar = data[offset - 1];
    this.isDirectory = lastChar === 47 || lastChar === 92;

    if (this.extraLen) {
      this.readExtra(data, offset);
      offset += this.extraLen;
    }
    this.comment = this.comLen ? data.slice(offset, offset + this.comLen).toString() : null;
  }

  validateName() {
    if (/\\|^\w+:|^\/|(^|\/)\.\.(\/|$)/.test(this.name)) {
      throw new Error('Malicious entry: ' + this.name);
    }
  }

  readExtra(data: Buffer, offset: number) {
    let signature, size;
    const maxPos = offset + this.extraLen;
    while (offset < maxPos) {
      signature = data.readUInt16LE(offset);
      offset += 2;
      size = data.readUInt16LE(offset);
      offset += 2;
      if (consts.ID_ZIP64 === signature) {
        this.parseZip64Extra(data, offset, size);
      }
      offset += size;
    }
  }

  parseZip64Extra(data: Buffer, offset: number, length: number) {
    if (length >= 8 && this.size === consts.EF_ZIP64_OR_32) {
      this.size = readUInt64LE(data, offset);
      offset += 8;
      length -= 8;
    }
    if (length >= 8 && this.compressedSize === consts.EF_ZIP64_OR_32) {
      this.compressedSize = readUInt64LE(data, offset);
      offset += 8;
      length -= 8;
    }
    if (length >= 8 && this.offset === consts.EF_ZIP64_OR_32) {
      this.offset = readUInt64LE(data, offset);
      offset += 8;
      length -= 8;
    }
    if (length >= 4 && this.diskStart === consts.EF_ZIP64_OR_16) {
      this.diskStart = data.readUInt32LE(offset);
      // offset += 4; length -= 4;
    }
  }

  get encrypted() {
    return (this.flags & consts.FLG_ENTRY_ENC) === consts.FLG_ENTRY_ENC;
  }

  get isFile() {
    return !this.isDirectory;
  }
}

class FileWindowBuffer {
  position = 0;
  buffer = Buffer.alloc(0);
  constructor(public readHandle: ReadHandle) {
  }

  async read(pos: number, length: number) {
    if (this.buffer.length < length) {
      this.buffer = Buffer.alloc(length);
    }
    this.position = pos;
    await this.readHandle.readComplete(this.buffer, 0, length, this.position);
  }

  async expandLeft(length: number) {
    this.buffer = Buffer.concat([Buffer.alloc(length), this.buffer]);
    this.position -= length;
    if (this.position < 0) {
      this.position = 0;
    }
    await this.readHandle.readComplete(this.buffer, 0, length, this.position);
  }

  async expandRight(length: number,) {
    const offset = this.buffer.length;
    this.buffer = Buffer.concat([this.buffer, Buffer.alloc(length)]);
    await this.readHandle.readComplete(this.buffer, offset, length, this.position + offset);
  }

  async moveRight(shift: number) {
    if (shift) {
      this.buffer.copy(this.buffer, 0, shift);
    }
    this.position += shift;
    await this.readHandle.readComplete(this.buffer, this.buffer.length - shift, shift, this.position + this.buffer.length - shift);
  }
}

function createVerifier(crc: number, size: number) {
  const verify = new Verifier(crc, size);
  return new Transform({
    transform: (data: any, unused: BufferEncoding, passThru: TransformCallback) => {
      let err;
      try {
        verify.data(data);
      } catch (e: any) {
        err = e;
      }
      passThru(err, data);
    }
  });
}

function createCrcTable() {
  const crcTable = [];
  const b = Buffer.alloc(4);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 8; --k >= 0;) {
      if ((c & 1) !== 0) {
        c = 0xedb88320 ^ (c >>> 1);
      } else {
        c = c >>> 1;
      }
    }
    if (c < 0) {
      b.writeInt32LE(c, 0);
      c = b.readUInt32LE(0);
    }
    crcTable[n] = c;
  }
  return crcTable;
}

const crcTable = createCrcTable();

class Verifier {
  state = {
    crc: ~0,
    size: 0,
  };

  constructor(public crc: number, public size: number) {
  }

  data(data: Buffer) {
    let crc = this.state.crc;
    let off = 0;
    let len = data.length;
    while (--len >= 0) {
      crc = crcTable[(crc ^ data[off++]) & 0xff] ^ (crc >>> 8);
    }
    this.state.crc = crc;
    this.state.size += data.length;
    if (this.state.size >= this.size) {
      const buf = Buffer.alloc(4);
      buf.writeInt32LE(~this.state.crc & 0xffffffff, 0);
      crc = buf.readUInt32LE(0);
      if (crc !== this.crc) {
        throw new Error(`Invalid CRC Expected: ${this.crc} found:${crc} `);
      }
      if (this.state.size !== this.size) {
        throw new Error('Invalid size');
      }
    }
  }
}

function parseZipTime(timebytes: number, datebytes: number) {
  const timebits = toBits(timebytes, 16);
  const datebits = toBits(datebytes, 16);

  const mt = {
    h: parseInt(timebits.slice(0, 5).join(''), 2),
    m: parseInt(timebits.slice(5, 11).join(''), 2),
    s: parseInt(timebits.slice(11, 16).join(''), 2) * 2,
    Y: parseInt(datebits.slice(0, 7).join(''), 2) + 1980,
    M: parseInt(datebits.slice(7, 11).join(''), 2),
    D: parseInt(datebits.slice(11, 16).join(''), 2),
  };
  const dt_str = [mt.Y, mt.M, mt.D].join('-') + ' ' + [mt.h, mt.m, mt.s].join(':') + ' GMT+0';
  return new Date(dt_str);
}

function toBits(dec: number, size: number) {
  let b = (dec >>> 0).toString(2);
  while (b.length < size) {
    b = '0' + b;
  }
  return b.split('');
}

function readUInt64LE(buffer: Buffer, offset: number) {
  return buffer.readUInt32LE(offset + 4) * 0x0000000100000000 + buffer.readUInt32LE(offset);
}
