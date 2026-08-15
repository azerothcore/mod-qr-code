DELETE FROM `command` WHERE `name` = 'account 2fa qrcode';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('account 2fa qrcode', 0, 'Syntax: .account 2fa qrcode [$token]\nShows a new two-factor key as a QR code, so you can scan it with your authenticator app instead of typing it.\nRun the command again with the token your app generates to turn two-factor authentication on. Nothing changes on the account until you do.');
