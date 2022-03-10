# Manually verifying vcpkg-ce updates

vcpkg-ce ships as an NPM package. Unfortunately, NPM doesn't provide a built-in
mechanism for authors to sign packages before publishing them, making supply
chain verification difficult. Instead, vcpkg-ce provides a detached PGP signature
and Authenticode signed security catalog for the `tgz` package file in every
GitHub release. To manually install or upgrade vcpkg-ce while verifying that the
application came from Microsoft, follow these steps.

## Windows

1. Download the latest vcpkg-ce release and signature from GitHub. Download both
files to a new directory.
    ```
    $ mkdir vcpkg-ce
    $ curl -LO https://github.com/microsoft/vcpkg-ce/releases/latest/download/ce.tgz.cat
    $ curl -LO https://github.com/microsoft/vcpkg-ce/releases/latest/download/ce.tgz
    ```
2. Verify the signature and content of the catalog using PowerShell.
    ```
    $ Test-FileCatalog .\ce.tgz.cat
    Valid
    ```
1. Install the package.
   ```
   $ npm install -g .\ce.tgz
   ```

## Linux

1. Ensure `gpg` is installed on your system.
1. Download Microsoft's public key from https://packages.microsoft.com/keys/microsoft.asc
and verify that the fingerprint matches the values shown [in the documentation](
https://docs.microsoft.com/en-us/windows-server/administration/linux-package-repository-for-microsoft-software#package-and-repository-signing-key)
(`BC52 8686 B50D 79E3 39D3 721C EB3E 94AD BE12 29CF` as of this writing).
    ```
    $ curl -LO https://packages.microsoft.com/keys/microsoft.asc
    $ gpg --show-keys microsoft.asc
    pub   rsa2048 2015-10-28 [SC]
        BC528686B50D79E339D3721CEB3E94ADBE1229CF
    uid                      Microsoft (Release signing) <gpgsecurity@microsoft.com>
    ```
1. Import Microsoft's public key.
    ```
    $ gpg --import microsoft.asc
    gpg: key EB3E94ADBE1229CF: public key "Microsoft (Release signing) <gpgsecurity@microsoft.com>" imported
    gpg: Total number processed: 1
    gpg:               imported: 1
    ```
1. Download the latest vcpkg-ce release and signature from GitHub.
    ```
    $ curl -LO https://github.com/microsoft/vcpkg-ce/releases/latest/download/ce.tgz
    $ curl -LO https://github.com/microsoft/vcpkg-ce/releases/latest/download/cec.tgz.asc
    ```
1. Verify the signature.
    ```
    $ gpg --verify ce.tgz.asc
    gpg: assuming signed data in 'ce.tgz'
    gpg: Signature made Tue Jun 29 15:23:01 2021 PDT
    gpg:                using RSA key EB3E94ADBE1229CF
    gpg: Good signature from "Microsoft (Release signing) <gpgsecurity@microsoft.com>" [unknown]
    gpg: WARNING: This key is not certified with a trusted signature!
    gpg:          There is no indication that the signature belongs to the owner.
    Primary key fingerprint: BC52 8686 B50D 79E3 39D3  721C EB3E 94AD BE12 29CF
    ```
1. Install the package.
   ```
   $ npm install -g ./ce.tgz
   ```