LIS Workbench offline upload (Windows 10/11, Windows PowerShell 5.1)

Copy this whole directory to the LAN computer. Double-click upload.cmd, or run:
  upload.cmd -Server SERVER -Database LIS_DATABASE -User ADMIN_USER

The database table dbo.LS_AS_LISWORK must already have been created manually.
No DDL, permission changes, business-table access, or database maintenance is performed.
The SQL Server account needs SELECT/INSERT/UPDATE/DELETE on this table only.
The password is prompted securely and is never saved in these materials.
The connection requires encryption and certificate validation. For an explicitly
approved self-signed server, pass -TrustServerCertificate. No silent TLS fallback.
For example, after independently verifying the LAN/test server:
  upload.cmd -Server SERVER -Database LIS_DATABASE -User ADMIN_USER -TrustServerCertificate
This flag applies to this connection only; encryption remains enabled.
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

The client uses encrypted connections with certificate validation. If the LAN
server has an explicitly approved self-signed certificate, the client can be
configured locally in ClientConfig.ini under [Update]: TrustServerCertificate=1.
This is an explicit opt-in; neither tool silently downgrades encryption.
Old clients must first use the complete installer to obtain the capable Updater.exe.
The database version must be newer than the installed client to test an actual update;
equal or older database versions produce no installation prompt.
