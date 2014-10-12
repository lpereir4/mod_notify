---

ProFTPd module mod_notify
=========================

The *mod_notify* module is designed to send an email notification upon
a successful file transfer to the FTP server.

Author
------

Please contact [Joseph Benden](mailto:joe@thrallingpenguin.com) with
any questions, comments, or suggestions regarding this module.

Directives
----------

* Notify
* NotifySubject
* NotifyBody
* NotifyFromName
* NotifyFromAddress

---

* **Notify**
 * syntax: Notify on
 * default: none
 * context: all
 * module: mod_notify
 * compatibility: 0.1

The **Notify** directive will configure if the *mod_notify* engine is
active. If no **Notify** directive is configured, then the module with
do no email notification sending.

* **NotifySubject**
 * syntax: NotifySubject "A new file has been uploaded"
 * default: none
 * context: all
 * module: mod_notify
 * compatibility: 0.1

The **NotifySubject** directive will configure the outgoing email's
subject line. If no **NotifySubject** directive is configured, then
there will be no subject in the email received from the module.

* **NotifyBody**
 * syntax: NotifyBody "A file called %n (size %s) has been uploaded."
 * default: none
 * context: all
 * module: mod_notify
 * compatibility: 0.1

The **NotifyBody** directive will configure the outgoing email's body
content. Certain escape sequences are permitted and perform variable
expansion based on the uploaded file. If no **NotifyBody** directive
is configured, then there will be no body in the email received from
the module.

The available escape sequences are as follows:

|Sequence|Description                                 |
|--------|--------------------------------------------|
|%f      |Filename of the uploaded file.              |
|%F      |Full path and filename of the uploaded file.|
|%s      |The size in bytes of the uploaded file.     |
|%n      |A newline character sequence.               |
|%%      |A literal '%' character.                    |

* **NotifyFromName**
 * syntax: NotifyFromName Joe
 * default: none
 * context: all
 * module: mod_notify
 * compatibility: 0.1

The **NotifyFromName** directive will configure the outgoing email's
user name (not their email address. See **NotifyFromAddress**.) If no
**NotifyFromName** directive is configured, then there will be no user
name in the email received from the module.

* **NotifyFromAddress**
 * syntax: NotifyFromAddress joe@example.com
 * default: none
 * context: all
 * module: mod_notify
 * compatibility: 0.1

The **NotifyFromAddress** directive will configure the outgoing
email's email address for the **NotifyFromUser** specified. If no
**NotifyFromAddress** directive is configured, then there will be no
email address in the email received from the module.

Installation
------------

To install *mod_notify*, copy the `mod_notify.c` file into:

    proftpd-dir/contrib/

after unpacking the proftpd source code. For including *mod_notify* as
a statically linked module:

    ./configure --with-modules=mod_notify

To build *mod_notify* as a DSO module:

    ./configure --enable-dso --with-shared=mod_notify

Then follow the usual steps:

    make
    make install

For those with an existing ProFTPd installation, you can use the
`prxs` tool to add *mod_notify*, as a DSO module, to your existing
server:

    prxs -c -i -d mod_notify.c

Example Configuration
---------------------

    <IfModule mod_notify.c>
      Notify on
      NotifySubject "You have received an upload."
      NotifyBody "The file %F (size %s) was uploaded."
      NotifyFromName Root
      NotifyFromAddress root@localhost
    </IfModule>
