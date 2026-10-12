LIS Workbench offline upload (Windows 10/11, Windows PowerShell 5.1)

Copy this whole directory to the LAN computer. Double-click upload.cmd, or run:
  upload.cmd -Server SERVER -Database LIS_DATABASE -User ADMIN_USER -TrustServerCertificate

The database table dbo.LS_AS_LISWORK must already have been created manually.
No DDL, permission changes, business-table access, or database maintenance is performed.
The SQL Server account needs SELECT/INSERT/UPDATE/DELETE on this table only.
The password is prompted securely and is never saved in these materials.
The connection always requires encryption. For LAN deployments it now trusts
server certificates by default, including when double-clicking upload.cmd.
-TrustServerCertificate remains supported but is optional; no encryption fallback.
To require certificate chain and hostname validation instead, run:
  upload.cmd -Server SERVER -Database LIS_DATABASE -User ADMIN_USER -ValidateServerCertificate
Certificate trust changes apply to this connection only; no server/system change.
Connection/login failure occurs before any SQL and gives a retry hint.
Do not change the system-wide PowerShell execution policy to run this tool.

The tool validates the ZIP, displays the actual target and version, and asks once.
Enter YES, yes or mixed case to confirm (surrounding spaces are ignored).
Press Enter or enter anything else to cancel without writing table data.
It uploads binary data, reads the stored content back to check SHA-256, then publishes.
An error prevents publication. It never deletes the current package.
After a new publication, retired records existing before this run and older than
24 hours are cleaned (at most five per operation). The newly retired previous version is kept. Cleanup failure
is reported separately and does not undo a successful publication.

02-publish.sql is for recovery by an administrator after a verified staged upload.
03-cleanup.sql previews candidates by default; set @Execute=1 to delete them.
Choose the correct LIS database manually before running either small SQL file.
Repeated upload of the current version is safe; conflicting content is rejected.
Do not edit a package or its metadata after it has been verified.
After updating the generator, move an existing version output directory aside,
then generate again and copy the WHOLE new directory. Replacing upload.ps1 alone
will fail the release-info.json checksum check.

The client also defaults to Encrypt=Yes and TrustServerCertificate=Yes for LIS
updates. To override locally in ClientConfig.ini under [Update]:
  TrustServerCertificate=1  (LAN default; also enables older clients to connect)
  TrustServerCertificate=0  (require certificate chain and hostname validation)
Existing explicit settings are preserved. Trust does not validate server identity.
Old clients must first use the complete installer to obtain the capable Updater.exe.
The database version must be newer than the installed client to test an actual update;
equal or older database versions produce no installation prompt.

Release versions: vYYYY.MM.DD, then vYYYY.MM.DD.1, .2, etc. on the same day.
Revision is a positive integer without leading zeros (maximum 999999999).
This tool accepts updater protocols 1.0.0 and 1.1.0; new packages use 1.1.0.
Existing clients must run the complete installer once for date-revision support.
The exact legacy tags v2026.10.13, v2026.10.14, v2026.10.15 compare as
v2026.10.12.1, .2, .3; package identities and stored data are not rewritten.
Always regenerate and copy the whole upload directory with the new tool.
No database schema migration is needed; the existing update table is reused.

If SQL error 195 occurs during publication, the package may already be staged
and verified. Keep it; do not change database compatibility or recreate the table.
Use the updated generator to regenerate the WHOLE directory for the same Release,
then retry upload.cmd. Matching version/hash reuses the staged record.
The publisher supports SQL Server 2012+ with older database compatibility levels.
It validates dates using TRY_CAST on unseparated YYYYMMDD.
