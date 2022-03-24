// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { parseVsManFromChannel, resolveVsManId, VsManDatabase } from '@microsoft/vcpkg-ce/dist/willow/willow';
import { strict, throws } from 'assert';
import { describe, it } from 'mocha';
import { SuiteLocal } from './SuiteLocal';

function testMalformedChannelParse(chmanContent: string, damage: (chmanObjects: any) => void) {
  const chmanObjects = JSON.parse(chmanContent);
  damage(chmanObjects);
  throws(() => parseVsManFromChannel(JSON.stringify(chmanObjects)));
}

describe('Willow', () => {
  // "channelItems": [
  //   {
  //     "id": "Microsoft.VisualStudio.Manifests.VisualStudio",
  //     "version": "16.0.31205.134",
  //     "type": "Manifest",
  //     "payloads": [
  //       {
  //         "fileName": "VisualStudio.vsman",
  //         "sha256": "6c99749816b688015ce25f72664610e3b5e163621a2e3dd19b06982b5c9cb31f",
  //         "size": 18413665,
  //         "url": "https://download.visualstudio.microsoft.com/download/pr/.../VisualStudio.vsman"
  //       }
  //     ]
  //   },

  it('Parses Channel Manifests', async () => {
    const local = new SuiteLocal();
    const chmanUri = local.resourcesFolderUri.join('2020-05-06-VisualStudio.16.Release.chman');
    const chmanContent = await chmanUri.readUTF8();
    const result = parseVsManFromChannel(chmanContent);
    strict.deepStrictEqual(result, {
      fileName: 'VisualStudio.vsman',
      sha256: '6c99749816b688015ce25f72664610e3b5e163621a2e3dd19b06982b5c9cb31f',
      size: 18413665,
      url: 'https://download.visualstudio.microsoft.com/download/pr/3105fcfe-e771-41d6-9a1c-fc971e7d03a7/6c99749816b688015ce25f72664610e3b5e163621a2e3dd19b06982b5c9cb31f/VisualStudio.vsman',
      version: '16.0.31205.134'
    });

    await local.after();
  });

  it('Errors On Bad Channel Manifests', async () => {
    throws(() => parseVsManFromChannel('{{{'));
    const local = new SuiteLocal();
    const chmanUri = local.resourcesFolderUri.join('2020-05-06-VisualStudio.16.Release.chman');
    const chmanContent = await chmanUri.readUTF8();
    const test = (damage: (chmanObjects: any) => void) => {
      testMalformedChannelParse(chmanContent, damage);
    };

    // bad version
    test((chmanObjects) => chmanObjects.manifestVersion = '0');
    // bad channelItems
    test((chmanObjects) => chmanObjects.channelItems = '0');
    // no Microsoft.VisualStudio.Manifests.VisualStudio
    test((chmanObjects) => chmanObjects.channelItems = chmanObjects.channelItems.slice(1));
    // too many Microsoft.VisualStudio.Manifests.VisualStudio
    test((chmanObjects) => chmanObjects.channelItems = chmanObjects.channelItems.push(chmanObjects.channelItems[0]));

    const testVsManEntry = (damage: (channelItem: any) => void) => {
      testMalformedChannelParse(chmanContent, (chmanObjects) => damage(chmanObjects.channelItems[0]));
    };

    // bad payloads
    testVsManEntry((entry) => entry.payloads = 0);
    // not manifest
    testVsManEntry((entry) => entry.type = 0);
    // version not string
    testVsManEntry((entry) => entry.version = 0);
    // multiple payloads
    testVsManEntry((entry) => entry.payloads = entry.payloads.push(entry.payloads[0]));
    // bad filename
    testVsManEntry((entry) => entry.payloads[0].fileName = 0);
    // bad sha
    testVsManEntry((entry) => entry.payloads[0].sha256 = 0);
    // bad sha content
    testVsManEntry((entry) => entry.payloads[0].sha256 = '6c99749816b688015ce25f726'); // length bad
    // bad size
    testVsManEntry((entry) => entry.payloads[0].size = '0');
    // bad url
    testVsManEntry((entry) => entry.payloads[0].url = 0);

    await local.after();
  });

  it('Parses VS Manifests', async () => {
    const local = new SuiteLocal();
    const vsmanUri = local.resourcesFolderUri.join('2021-05-06-VisualStudio.vsman');
    const vsmanContent = await vsmanUri.readUTF8();
    const result = new VsManDatabase(vsmanContent);
    strict.deepStrictEqual(result.get('Microsoft.VisualCpp.Tools.HostX86.TargetX86'),
      [
        {
          'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86',
          'version': '14.28.29914',
          'language': undefined,
          'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.vsix',
          'sha256': '5b9e9d7f332e79bcc2342c49e7f99ddf43ecf6d130e5e6c6908dc2c7a39b15c6',
          'size': 16126219,
          'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/5b9e9d7f332e79bcc2342c49e7f99ddf43ecf6d130e5e6c6908dc2c7a39b15c6/Microsoft.VisualCpp.Tools.HostX86.TargetX86.vsix',
          'installSize': 35276532,
          'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86,version=14.28.29914/payload.vsix'
        }
      ]);

    strict.deepStrictEqual(result.get('Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources'), [
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'cs-CZ',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.csy.vsix',
        'sha256': '47db023518b45bab6e7b9bba9aa2b0254895f3eb21221a70bdeb453e2747bafd',
        'size': 229756,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/47db023518b45bab6e7b9bba9aa2b0254895f3eb21221a70bdeb453e2747bafd/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.csy.vsix',
        'installSize': 906288,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=cs-CZ/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'de-DE',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.deu.vsix',
        'sha256': '5e128ff82600b53cebd63afaa40d3714d52c539104bf8ad4b44d61f261e933a1',
        'size': 234819,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/5e128ff82600b53cebd63afaa40d3714d52c539104bf8ad4b44d61f261e933a1/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.deu.vsix',
        'installSize': 1026640,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=de-DE/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'en-US',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.enu.vsix',
        'sha256': '4752b7e2053d1db888b43309d604ae7ed807d363ecbb39a89937ca051a10bdc8',
        'size': 204190,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/4752b7e2053d1db888b43309d604ae7ed807d363ecbb39a89937ca051a10bdc8/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.enu.vsix',
        'installSize': 862328,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=en-US/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'es-ES',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.esn.vsix',
        'sha256': 'b9c8500e4f49df54292c7aab3bae930a940cd264877b4feee7cc26618f99edf4',
        'size': 224027,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/b9c8500e4f49df54292c7aab3bae930a940cd264877b4feee7cc26618f99edf4/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.esn.vsix',
        'installSize': 1001560,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=es-ES/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'fr-FR',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.fra.vsix',
        'sha256': 'fe41ec5957aaa14e5a613cd2352bc1171b1e4b550e980bf4dcd0b11fce1d7192',
        'size': 225466,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/fe41ec5957aaa14e5a613cd2352bc1171b1e4b550e980bf4dcd0b11fce1d7192/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.fra.vsix',
        'installSize': 1012800,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=fr-FR/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'it-IT',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.ita.vsix',
        'sha256': '8c540bc9953a7f1c26072e16748926a73818ae5fb5b8288bedbc0841e789bfac',
        'size': 222846,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/8c540bc9953a7f1c26072e16748926a73818ae5fb5b8288bedbc0841e789bfac/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.ita.vsix',
        'installSize': 1012272,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=it-IT/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'ja-JP',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.jpn.vsix',
        'sha256': '91e4c239726a104a38c7ae69d1590abc20249354c0fd5a5ae580ceef8e5149eb',
        'size': 200323,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/91e4c239726a104a38c7ae69d1590abc20249354c0fd5a5ae580ceef8e5149eb/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.jpn.vsix',
        'installSize': 636488,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=ja-JP/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'ko-KR',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.kor.vsix',
        'sha256': '3b1fa4d736a7d82bef9054fbe5f52919d34a8c5ec614e3d957e7665b94ae9ece',
        'size': 195053,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/3b1fa4d736a7d82bef9054fbe5f52919d34a8c5ec614e3d957e7665b94ae9ece/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.kor.vsix',
        'installSize': 640568,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=ko-KR/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'pl-PL',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.plk.vsix',
        'sha256': '13b2331b7157e21006b79c873d532e5e256ae049f41b11be972e316eae6a8a1b',
        'size': 233518,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/13b2331b7157e21006b79c873d532e5e256ae049f41b11be972e316eae6a8a1b/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.plk.vsix',
        'installSize': 982080,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=pl-PL/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'pt-BR',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.ptb.vsix',
        'sha256': '2d4133d05cf9adec2bdbc73cbdb266c359d7bb1fe6e012cc346b1dff334df860',
        'size': 219322,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/2d4133d05cf9adec2bdbc73cbdb266c359d7bb1fe6e012cc346b1dff334df860/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.ptb.vsix',
        'installSize': 947784,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=pt-BR/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'ru-RU',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.rus.vsix',
        'sha256': '54d321dc5471c42d655aa5b613b55dab3f1129df179bef8597e470d46f464445',
        'size': 233022,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/54d321dc5471c42d655aa5b613b55dab3f1129df179bef8597e470d46f464445/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.rus.vsix',
        'installSize': 968256,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=ru-RU/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'tr-TR',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.trk.vsix',
        'sha256': '7b1cbe7df387e5132d8e639e407afe8c83b45f624cde06fc9aa6af0bf896f32d',
        'size': 216776,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7b1cbe7df387e5132d8e639e407afe8c83b45f624cde06fc9aa6af0bf896f32d/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.trk.vsix',
        'installSize': 911416,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=tr-TR/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'zh-CN',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.chs.vsix',
        'sha256': '7538c083be1f37b91217b8690a72746fc57bcd4c0e8c21f553ac4f88f8c60c98',
        'size': 183804,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7538c083be1f37b91217b8690a72746fc57bcd4c0e8c21f553ac4f88f8c60c98/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.chs.vsix',
        'installSize': 478256,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=zh-CN/payload.vsix'
      },
      {
        'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources',
        'version': '14.28.29914',
        'language': 'zh-TW',
        'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.cht.vsix',
        'sha256': 'f0c019c004dcca5f7f5ce3a9b97730967cfff6f960113a55f7f317a41ade3df7',
        'size': 187675,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/f0c019c004dcca5f7f5ce3a9b97730967cfff6f960113a55f7f317a41ade3df7/Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources.cht.vsix',
        'installSize': 503864,
        'localPath': 'vsix:///Microsoft.VisualCpp.Tools.HostX86.TargetX86.Resources,version=14.28.29914,language=zh-TW/payload.vsix'
      }
    ]);

    await local.after();
  });

  it('Hard Rejects Completely Broken VS Manifests', () => {
    throws(() => { new VsManDatabase('{'); });
    throws(() => { new VsManDatabase('{}'); });
    throws(() => { new VsManDatabase('{\'packages\': 42}'); });
  });

  it('Ignores Unusuable Packages', () => {
    const testInput = {
      'packages': [
        {
          'id': 'good',
          'version': '14.28.29914',
          'type': 'Vsix',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 33650830
          }
        },
        {
          // no id
          'version': '14.28.29914',
          'type': 'Vsix',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 33650830
          }
        },
        {
          'id': 'badversion',
          'version': 42,
          'type': 'Vsix',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 33650830
          }
        },
        {
          'id': 'badtype',
          'version': '14.28.29914',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 33650830
          }
        },
        {
          'id': 'badinstallsize',
          'version': '14.28.29914',
          'type': 'Vsix',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'elsewhere': 33650830
          }
        },
        {
          'id': 'badpayload',
          'version': '14.28.29914',
          'type': 'Vsix',
          'payloads': [
            {
              'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 33650830
          }
        },
        {
          'id': 'multipayload',
          'version': '14.28.29914',
          'type': 'Vsix',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
              'signer': {
                '$ref': '2'
              }
            },
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix_other',
              'sha256': 'xxxx3eaab5d6000185cf8c44e3eeb8229cd1afa3dda2cf1d9c579f9125561a42',
              'size': 15447514,
              'url': 'https://example.com/hello.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 33650830
          }
        }
      ]
    };

    const actual = new VsManDatabase(JSON.stringify(testInput));
    strict.equal(actual.size, 1);
    strict.deepStrictEqual(actual.get('good'), [{
      'id': 'good',
      'version': '14.28.29914',
      'language': undefined,
      'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
      'sha256': '7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42',
      'size': 15447514,
      'url': 'https://download.visualstudio.microsoft.com/download/pr/3c309edd-88c5-4207-ab8d-fc1fda49d203/7c3a3eaab5d6000185cf8c44e3eeb8229cd1afa3dd72cf1d9c579f9125561a42/Microsoft.VisualCpp.Tools.HostX86.TargetX64.vsix',
      'installSize': 33650830,
      'localPath': 'vsix:///good,version=14.28.29914/payload.vsix'
    }]);
  });

  it('Follows Pointer-like Packages', () => {
    const testInput = {
      'packages': [
        {
          'id': 'Microsoft.VisualCpp.ASAN.X86',
          'version': '14.29.30037',
          'type': 'Vsix',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.ASAN.X86.vsix',
              'sha256': '97c926de9fef5e5c8760638ad05ceaa7793f923ab608ece35c9102c7cc992338',
              'size': 1071,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/97c926de9fef5e5c8760638ad05ceaa7793f923ab608ece35c9102c7cc992338/Microsoft.VisualCpp.ASAN.X86.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'dependencies': {
            'Microsoft.VC.14.29.16.10.ASAN.X86.base': '14.29.30037'
          }
        },
        {
          'id': 'Microsoft.VC.14.29.16.10.ASAN.X86.base',
          'version': '14.29.30037',
          'type': 'Vsix',
          'payloads': [
            {
              'fileName': 'Microsoft.VC.14.29.16.10.ASAN.X86.base.vsix',
              'sha256': '4a39aa98ee4540b8cd9a55ad6f1717602ee45ffd2db3a70894b9a3b41dfdac1c',
              'size': 17401493,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/4a39aa98ee4540b8cd9a55ad6f1717602ee45ffd2db3a70894b9a3b41dfdac1c/Microsoft.VC.14.29.16.10.ASAN.X86.base.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 63068674
          },
          'dependencies': {
            'Microsoft.VC.14.29.16.10.ASAN.Headers.base': '14.29.30037',
            'Microsoft.VC.14.29.16.10.ASAN.X64.base': '14.29.30037',
            'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetX86.base': '14.29.30037',
            'Microsoft.VC.14.29.16.10.Tools.HostX64.TargetX86.base': '14.29.30037'
          }
        },
        {
          'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetARM.Resources',
          'version': '14.29.30037',
          'type': 'Vsix',
          'language': 'de-DE',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetARM.Resources.deu.vsix',
              'sha256': 'a4cf6d5ac182a7f386afabdcb27605fca34869bc2b6bd39c4210021f85813599',
              'size': 1100,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/a4cf6d5ac182a7f386afabdcb27605fca34869bc2b6bd39c4210021f85813599/Microsoft.VisualCpp.Tools.HostX86.TargetARM.Resources.deu.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'dependencies': {
            'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base': '14.29.30037'
          }
        },
        {
          'id': 'Microsoft.VisualCpp.Tools.HostX86.TargetARM.Resources',
          'version': '14.29.30037',
          'type': 'Vsix',
          'language': 'en-US',
          'payloads': [
            {
              'fileName': 'Microsoft.VisualCpp.Tools.HostX86.TargetARM.Resources.enu.vsix',
              'sha256': '0b83f64f4a9d9bc5200092d06117a0e7936ca7f1a4e3b596537cdba2893504d5',
              'size': 1100,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/0b83f64f4a9d9bc5200092d06117a0e7936ca7f1a4e3b596537cdba2893504d5/Microsoft.VisualCpp.Tools.HostX86.TargetARM.Resources.enu.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'dependencies': {
            'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base': '14.29.30037'
          }
        },
        {
          'id': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base',
          'version': '14.29.30037',
          'type': 'Vsix',
          'language': 'de-DE',
          'payloads': [
            {
              'fileName': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.deu.vsix',
              'sha256': '23612dd70c849c6735b17ce2887edf6d7ca243e9a2f9bd427daace8653b17987',
              'size': 236777,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/23612dd70c849c6735b17ce2887edf6d7ca243e9a2f9bd427daace8653b17987/Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.deu.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 1033240
          }
        },
        {
          'id': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base',
          'version': '14.29.30037',
          'type': 'Vsix',
          'language': 'en-US',
          'payloads': [
            {
              'fileName': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.enu.vsix',
              'sha256': 'b6c39771b8c5564f9998e7b1bd3a89e1d23d287f6625ef65f2cd43c1ccabd306',
              'size': 205517,
              'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/b6c39771b8c5564f9998e7b1bd3a89e1d23d287f6625ef65f2cd43c1ccabd306/Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.enu.vsix',
              'signer': {
                '$ref': '2'
              }
            }
          ],
          'installSizes': {
            'targetDrive': 868400
          }
        }
      ]
    };

    const actual = new VsManDatabase(JSON.stringify(testInput));
    strict.equal(actual.size, 4);
    const asanExpected = [{
      'id': 'Microsoft.VC.14.29.16.10.ASAN.X86.base',
      'version': '14.29.30037',
      'language': undefined,
      'fileName': 'Microsoft.VC.14.29.16.10.ASAN.X86.base.vsix',
      'sha256': '4a39aa98ee4540b8cd9a55ad6f1717602ee45ffd2db3a70894b9a3b41dfdac1c',
      'size': 17401493,
      'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/4a39aa98ee4540b8cd9a55ad6f1717602ee45ffd2db3a70894b9a3b41dfdac1c/Microsoft.VC.14.29.16.10.ASAN.X86.base.vsix',
      'installSize': 63068674,
      'localPath': 'vsix:///Microsoft.VC.14.29.16.10.ASAN.X86.base,version=14.29.30037/payload.vsix'
    }];
    strict.deepStrictEqual(actual.get('Microsoft.VisualCpp.ASAN.X86'), asanExpected);
    strict.deepStrictEqual(actual.get('Microsoft.VC.$(SxSVersion).ASAN.X86.base'), asanExpected);
    strict.deepStrictEqual(actual.get('Microsoft.VisualCpp.Tools.HostX86.TargetARM.Resources'), [
      {
        'id': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base',
        'version': '14.29.30037',
        'language': 'de-DE',
        'fileName': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.deu.vsix',
        'sha256': '23612dd70c849c6735b17ce2887edf6d7ca243e9a2f9bd427daace8653b17987',
        'size': 236777,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/23612dd70c849c6735b17ce2887edf6d7ca243e9a2f9bd427daace8653b17987/Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.deu.vsix',
        'installSize': 1033240,
        'localPath': 'vsix:///Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base,version=14.29.30037,language=de-DE/payload.vsix'
      },
      {
        'id': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base',
        'version': '14.29.30037',
        'language': 'en-US',
        'fileName': 'Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.enu.vsix',
        'sha256': 'b6c39771b8c5564f9998e7b1bd3a89e1d23d287f6625ef65f2cd43c1ccabd306',
        'size': 205517,
        'url': 'https://download.visualstudio.microsoft.com/download/pr/c0ac19c1-e1d7-47e2-bde8-fd11c4410cca/b6c39771b8c5564f9998e7b1bd3a89e1d23d287f6625ef65f2cd43c1ccabd306/Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base.enu.vsix',
        'installSize': 868400,
        'localPath': 'vsix:///Microsoft.VC.14.29.16.10.Tools.HostX86.TargetARM.Resources.base,version=14.29.30037,language=en-US/payload.vsix'
      }
    ]);
  });

  it('Finds Latest Numbers', () => {
    strict.equal(resolveVsManId(['hello.100.world', 'hello.9.world', 'hello.10.world', 'unrelated'], 'hello.$(SxSVersion).world'), 'hello.100.world');

    strict.equal(resolveVsManId(['hello.9.world', 'hello.10.world', 'unrelated', 'hello.100.world'], 'hello.$(SxSVersion).world'), 'hello.100.world');

    strict.equal(resolveVsManId(['hello.9.world', 'hello.100.world', 'hello.10.world', 'unrelated'], 'hello.$(SxSVersion).world'), 'hello.100.world');

    strict.equal(resolveVsManId(['hello.100.10.world', 'hello.100.world', 'hello.10.world', 'unrelated'], 'hello.$(SxSVersion).world'), 'hello.100.10.world');

    strict.equal(resolveVsManId(['hello.100.10.world', 'hello.100.world', 'hello.10.world', 'unrelated'], '$(SxSVersion)'), '$(SxSVersion)');

    strict.equal(resolveVsManId(['100.10', '100', '10', 'unrelated'], '$(SxSVersion)'), '100.10');
  });
});
