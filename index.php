<html>
<head>
	<title>Telegram-CLI Build Artifacts</title>
</head>
<body>
<?php
echo "<h1>Telegram Messenger Command Line Client</h1>\n";
echo "<h2>Build Artifacts</h2>\n";
echo "Download <a href=\"./tg-server.pub\">tg-server.pub</a> and save it as /etc/telegram/server.pub<br/>\n";
echo "<br/>\n";

$dir = $_SERVER['OPENSHIFT_HOMEDIR']."app-root/data/";
foreach(scandir($dir, SCANDIR_SORT_DESCENDING) as $build)
{
	if( $build == "." OR $build == ".." OR $build == ".bash_history" OR $build == ".bash_profile" OR $build == ".vimrc" )
		continue;

	echo "Build: ".$build."<br/>\n";
	foreach(scandir($dir.$build."/") as $system)
	{
		if( $system == "." OR $system == ".." )
			continue;

		echo "> ".$system."<br/>\n";
		foreach(scandir($dir.$build."/".$system."/") as $file)
		{
			if( $file == "." OR $file == ".." OR $file == "config.log" )
				continue;

			if(!is_file("./artifacts/".$build."/".$system."/".$file))
			{
				mkdir("./artifacts/".$build."/".$system."/", 0777, true);
				copy($dir.$build."/".$system."/".$file, "./artifacts/".$build."/".$system."/".$file);
			}
			echo ">> <a href=\"./artifacts/".$build."/".$system."/".$file."\">".$file."</a><br/>\n";
		}
	}
	echo "<br/>\n";
}
?>
</body>
</html>
