#!"D:\AppData\xampp\perl\bin\perl.exe"
##
##  printenv -- demo CGI program which just prints its environment
##
use IO::Socket::INET;
use CGI qw(:standard);
use strict;

# auto-flush on socket
$| = 1;

print "Content-type: text/plain; charset=iso-8859-1\n\n";

my $query = new CGI;
my $command = $query->param('cmd');

my $socket = new IO::Socket::INET (
    PeerHost => 'localhost',
    PeerPort => '30000',
    Proto => 'tcp',
);
die "cannot connect to the server $!\n" unless $socket;
#print "connected to the server\n";

# data to send to a server
my $req = 'balance';
#my $size = $socket->send($req);
#print "sent data of length $size\n";
print $socket "$command\n";
 
# notify server that request has been sent
shutdown($socket, 1);
 
# receive a response of up to 1024 characters from server
my $response;
while ($response = <$socket>) {
	   print $response;
}
 
$socket->close();