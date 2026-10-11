<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>AboutDialog</name>
    <message>
        <location filename="../AboutDialog.ui" line="+14"/>
        <source>About {appname}</source>
        <translation>关于 {appname}</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Copyright © The Retransmission Project</source>
        <translation>Copyright © The Retransmission Project</translation>
    </message>
    <message>
        <source>Copyright © The {appname} Project</source>
        <translation>Copyright © The {appname} Project</translation>
    </message>
    <message>
        <location line="-10"/>
        <source>A fast and easy BitTorrent client</source>
        <translation>一个快速、简单的 BitTorrent 客户端</translation>
    </message>
    <message>
        <location filename="../AboutDialog.cc" line="+37"/>
        <source>Client</source>
        <translation>客户端</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Server</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>C&amp;redits</source>
        <translation>致谢(&amp;R)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&amp;License</source>
        <translation>许可协议(&amp;L)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Credits</source>
        <translation>致谢</translation>
    </message>
</context>
<context>
    <name>Application</name>
    <message numerus="yes">
        <location filename="../Application.cc" line="+313"/>
        <source>Torrent(s) Completed</source>
        <translation>
            <numerusform>种子已完成</numerusform>
        </translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Torrent Added</source>
        <translation>种子已添加</translation>
    </message>
</context>
<context>
    <name>DetailsDialog</name>
    <message>
        <location filename="../DetailsDialog.cc" line="+469"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mixed</source>
        <translation>混合</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+268"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-210"/>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>{downloaded_size} (+{discarded_size} discarded after failed checksum)</source>
        <translation>{downloaded_size} (+{discarded_size} 校验失败后丢弃)</translation>
    </message>
    <message>
        <location line="+130"/>
        <source>Active now</source>
        <translation>当前活动</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>{time_span} ago</source>
        <translation>{time_span} 之前</translation>
    </message>
    <message numerus="yes">
        <location line="+63"/>
        <source>{total_size} ({piece_count:L} pieces @ {piece_size})</source>
        <translation>
            <numerusform>{total_size} ({piece_count:L} 个区块 @ {piece_size})</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>{total_size} ({piece_count:L} pieces)</source>
        <translation>
            <numerusform>{total_size} ({piece_count:L} 个区块)</numerusform>
        </translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Private to this tracker -- DHT and PEX disabled</source>
        <translation>当前 Tracker 设置为私有 -- DHT 和 PEX 被禁用</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Public torrent</source>
        <translation>公共种子</translation>
    </message>
    <message>
        <location line="+102"/>
        <source>Created by {creator}</source>
        <translation>通过 {creator} 创建</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Created on {date}</source>
        <translation>创建于 {date}</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Created by {creator} on {date}</source>
        <translation>通过 {creator} 创建于 {date}</translation>
    </message>
    <message>
        <location line="+207"/>
        <location line="+47"/>
        <source>Encrypted connection</source>
        <translation>加密连接</translation>
    </message>
    <message>
        <location line="-28"/>
        <source>Optimistic unchoke</source>
        <translation>开放式unchoke</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloading from this peer</source>
        <translation>正在从该节点下载</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>We would download from this peer if they would let us</source>
        <translation>如果对方允许，我们将从该节点下载</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Uploading to peer</source>
        <translation>正在上传给该节点</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>We would upload to this peer if they asked</source>
        <translation>如果对方请求，我们将上传给该节点</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Peer has unchoked us, but we&apos;re not interested</source>
        <translation>节点已对我们开放，但我们不感兴趣</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>We unchoked this peer, but they&apos;re not interested</source>
        <translation>我们已对此节点开放，但对方不感兴趣</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Peer was discovered through DHT</source>
        <translation>通过 DHT 发现的节点</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Peer was discovered through Peer Exchange (PEX)</source>
        <translation>通过节点交换（PEX）发现的节点</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Peer is an incoming connection</source>
        <translation>节点是一个传入连接</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Peer is connected over µTP</source>
        <translation>通过 µTP 连接的节点</translation>
    </message>
    <message>
        <location line="+161"/>
        <source>Add tracker announce URLs, one per line:</source>
        <translation>添加 Tracker 宣告网址，每个一行：</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No new URLs found.</source>
        <translation>没有找到新的网址。</translation>
    </message>
    <message>
        <location line="-872"/>
        <source>{current_size} (100%)</source>
        <extracomment>Text following the &quot;Have:&quot; label in torrent properties dialog; {current_size} is amount of downloaded and verified data</extracomment>
        <translation>{current_size} (100%)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>{current_size} of {complete_size} ({percent_done}%)</source>
        <extracomment>Text following the &quot;Have:&quot; label in torrent properties dialog; {current_size} is amount of downloaded and verified data, {complete_size} is overall size of torrent data, {percent_done} is percentage ({current_size}/{complete_size}*100)</extracomment>
        <translation>{current_size} / {complete_size} ({percent_done}%)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>{current_size} of {complete_size} ({percent_done}%), {unverified_size} Unverified</source>
        <extracomment>Text following the &quot;Have:&quot; label in torrent properties dialog; {current_size} is amount of downloaded data (both verified and unverified), {complete_size} is overall size of torrent data, {percent_done} is percentage ({current_size}/{complete_size}*100), {unverified_size} is amount of downloaded but not yet verified data</extracomment>
        <translation>{current_size} / {complete_size} ({percent_done}%), {unverified_size} 未验证</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>{uploaded_size} (Ratio: {ratio})</source>
        <translation>{uploaded_size} (分享率: {ratio})</translation>
    </message>
    <message>
        <location line="+303"/>
        <location line="+55"/>
        <source>N/A</source>
        <translation>不可用</translation>
    </message>
    <message numerus="yes">
        <location line="+358"/>
        <source>{minutes:L} minute(s)</source>
        <extracomment>Spin box format, &quot;Stop seeding if idle for: [ 5 minutes ]&quot;</extracomment>
        <translation>
            <numerusform>{minutes:L} 分钟</numerusform>
        </translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Add URL(s)</source>
        <translation>添加网址</translation>
    </message>
    <message>
        <location line="+116"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+4"/>
        <source>Use Global Settings</source>
        <translation>使用全局设置</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Seed regardless of ratio</source>
        <translation>无视分享率做种</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop seeding at ratio:</source>
        <translation>停止做种分享率:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Seed regardless of activity</source>
        <translation>无视活跃度做种</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop seeding if idle for:</source>
        <translation>停止做种闲置时间:</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Up</source>
        <translation>上传</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Down</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%</source>
        <translation>%</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Address</source>
        <translation>地址</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Client</source>
        <translation>客户端</translation>
    </message>
    <message>
        <location filename="../DetailsDialog.ui" line="+14"/>
        <source>Torrent Properties</source>
        <translation>种子属性</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Information</source>
        <translation>信息</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Activity</source>
        <translation>活动</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Have:</source>
        <translation>已有：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Availability:</source>
        <translation>可用性：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Uploaded:</source>
        <translation>已上传：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Downloaded:</source>
        <translation>已下载：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>State:</source>
        <translation>状态：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Running time:</source>
        <translation>运行时间：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Remaining time:</source>
        <translation>剩余时间：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Last activity:</source>
        <translation>最后活动：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Error:</source>
        <translation>错误：</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Details</source>
        <translation>详细信息</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Size:</source>
        <translation>大小：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Location:</source>
        <translation>位置:</translation>
    </message>
    <message>
        <location line="+154"/>
        <source>Labels:</source>
        <translation>标签：</translation>
    </message>
    <message>
        <location line="-122"/>
        <source>Hash:</source>
        <translation>哈希值：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Privacy:</source>
        <translation>隐私性：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Origin:</source>
        <translation>来源：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Added:</source>
        <translation>添加:</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Comment:</source>
        <translation>备注：</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Peers</source>
        <translation>节点</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Tracker</source>
        <translation>Tracker</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Show &amp;more details</source>
        <translation>显示更多细节(&amp;M)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show &amp;backup trackers</source>
        <translation>显示备用 Tracker(&amp;B)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Honor global &amp;limits</source>
        <translation>遵循全局限制(&amp;L)</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Limit &amp;download speed:</source>
        <translation>下载限速(&amp;D)：</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Limit &amp;upload speed:</source>
        <translation>上传限速(&amp;U)：</translation>
    </message>
    <message>
        <location line="-100"/>
        <source>Add</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Edit</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Remove</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location line="+126"/>
        <source>Torrent &amp;priority:</source>
        <translation>种子优先级(&amp;P)：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Seeding Limits</source>
        <translation>做种限制</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>&amp;Ratio:</source>
        <translation>分享率(&amp;R)：</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>&amp;Idle:</source>
        <translation>闲置(&amp;I)：</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Peer Connections</source>
        <translation>节点连接</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>&amp;Maximum peers:</source>
        <translation>最大节点数(&amp;M)：</translation>
    </message>
    <message>
        <source>{torrent_name} - Edit Trackers</source>
        <translation>{torrent_name}—编辑 Tracker 列表</translation>
    </message>
    <message>
        <source>&amp;Add</source>
        <translation>添加(&amp;A)</translation>
    </message>
    <message>
        <source>&amp;Edit</source>
        <translation>编辑(&amp;E)</translation>
    </message>
    <message>
        <source>&amp;Remove</source>
        <translation>移除(&amp;R)</translation>
    </message>
</context>
<context>
    <name>FileTreeItem</name>
    <message>
        <location filename="../FileTreeItem.cc" line="+279"/>
        <location filename="../FileTreeView.cc" line="+110"/>
        <location line="+258"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+3"/>
        <location filename="../FileTreeView.cc" line="-258"/>
        <location line="+256"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+3"/>
        <location filename="../FileTreeView.cc" line="-256"/>
        <location line="+257"/>
        <source>Normal</source>
        <translation>中</translation>
    </message>
    <message>
        <location line="+3"/>
        <location filename="../FileTreeView.cc" line="-256"/>
        <source>Mixed</source>
        <translation>混合</translation>
    </message>
</context>
<context>
    <name>FileTreeModel</name>
    <message>
        <location filename="../FileTreeModel.cc" line="+203"/>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
</context>
<context>
    <name>FileTreeView</name>
    <message>
        <location filename="../FileTreeView.cc" line="+248"/>
        <source>Check Selected</source>
        <translation>勾选</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uncheck Selected</source>
        <translation>取消勾选</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Only Check Selected</source>
        <translation>仅勾选</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Open</source>
        <translation>打开</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rename…</source>
        <translation>重命名…</translation>
    </message>
</context>
<context>
    <name>FilterBar</name>
    <message>
        <location filename="../FilterBar.cc" line="+47"/>
        <location line="+138"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="-125"/>
        <source>Active</source>
        <translation>活动</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Seeding</source>
        <translation>正在做种</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying</source>
        <translation>正在验证</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="+142"/>
        <source>Show:</source>
        <translation>显示：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Search…</source>
        <translation>搜索…</translation>
    </message>
    <message>
        <source>&amp;Show:</source>
        <translation>显示(&amp;S)：</translation>
    </message>
</context>
<context>
    <name>Formatter</name>
    <message>
        <location filename="../Formatter.cc" line="+19"/>
        <location line="+25"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-20"/>
        <location line="+10"/>
        <location line="+19"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message numerus="yes">
        <location line="+30"/>
        <source>{days:L} day(s)</source>
        <translation>
            <numerusform>{days:L} 天</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="-5"/>
        <source>{hours:L} hour(s)</source>
        <translation>
            <numerusform>{hours:L} 时</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="-7"/>
        <source>{minutes:L} minute(s)</source>
        <translation>
            <numerusform>{minutes:L} 分</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="-7"/>
        <source>{seconds:L} second(s)</source>
        <translation>
            <numerusform>{seconds:L} 秒</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>FreeSpaceLabel</name>
    <message>
        <location filename="../FreeSpaceLabel.cc" line="+58"/>
        <source>&lt;i&gt;Calculating Free Space…&lt;/i&gt;</source>
        <translation>&lt;i&gt;正在计算可用空间…&lt;/i&gt;</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>{disk_space} free</source>
        <translation>{disk_space} 可用</translation>
    </message>
</context>
<context>
    <name>LicenseDialog</name>
    <message>
        <location filename="../LicenseDialog.ui" line="+14"/>
        <source>License</source>
        <translation>许可协议</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../MainWindow.ui" line="+14"/>
        <source>Retransmission</source>
        <translation>Retransmission</translation>
    </message>
    <message>
        <location line="+181"/>
        <source>&amp;Torrent</source>
        <translation>种子(&amp;T)</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>&amp;Edit</source>
        <translation>编辑(&amp;E)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;Help</source>
        <translation>帮助(&amp;H)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;View</source>
        <translation>视图(&amp;V)</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>&amp;File</source>
        <translation>文件(&amp;F)</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Create a new torrent</source>
        <translation>创建一个新种子</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Properties</source>
        <translation>属性(&amp;P)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show torrent properties</source>
        <translation>显示种子属性</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Open the torrent&apos;s folder</source>
        <translation>打开种子文件夹</translation>
    </message>
    <message>
        <location line="-161"/>
        <source>Queue</source>
        <translation>队列</translation>
    </message>
    <message>
        <location line="-132"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+83"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location line="+171"/>
        <source>&amp;Open…</source>
        <translation>打开(&amp;O)…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open</source>
        <translation>打开</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open a torrent</source>
        <translation>打开种子</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;New…</source>
        <translation>新建(&amp;N)…</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Open Fold&amp;er</source>
        <translation>打开文件夹(&amp;E)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;Start</source>
        <translation>开始(&amp;S)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start torrent</source>
        <translation>开始种子</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Ask Tracker for &amp;More Peers</source>
        <translation>向 Tracker 请求更多节点(&amp;M)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ask tracker for more peers</source>
        <translation>向 Tracker 请求更多节点</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>&amp;Pause</source>
        <translation>暂停(&amp;P)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pause torrent</source>
        <translation>暂停种子</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;Verify Local Data</source>
        <translation>验证本地数据(&amp;V)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Verify local data</source>
        <translation>验证本地数据</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Remove</source>
        <translation>移除(&amp;R)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove torrent</source>
        <translation>移除种子</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;Delete Files and Remove</source>
        <translation>删除文件并移除(&amp;D)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove torrent and delete its files</source>
        <translation>移除种子并删除文件</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Start All</source>
        <translation>全部开始(&amp;S)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>&amp;Pause All</source>
        <translation>全部暂停(&amp;P)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>&amp;Quit</source>
        <translation>退出(&amp;Q)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;Select All</source>
        <translation>全选(&amp;S)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Deselect All</source>
        <translation>全消(&amp;D)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Preferences</source>
        <translation>偏好设置(&amp;P)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;Compact View</source>
        <translation>紧凑视图(&amp;C)</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+3"/>
        <source>Compact View</source>
        <translation>紧凑视图</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>&amp;Toolbar</source>
        <translation>工具栏(&amp;T)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Filterbar</source>
        <translation>筛选栏(&amp;F)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Statusbar</source>
        <translation>状态栏(&amp;S)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by &amp;Activity</source>
        <translation>按活动状态排序(&amp;A)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by A&amp;ge</source>
        <translation>按创建时间排序(&amp;G)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by Time &amp;Left</source>
        <translation>按剩余时间排序(&amp;L)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by &amp;Name</source>
        <translation>按名称排序(&amp;N)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by &amp;Progress</source>
        <translation>按进度排序(&amp;P)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by Rati&amp;o</source>
        <translation>按分享率排序(&amp;O)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by Si&amp;ze</source>
        <translation>按大小排序(&amp;Z)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by Stat&amp;e</source>
        <translation>按状态排序(&amp;E)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by T&amp;racker</source>
        <translation>按 Tracker 排序(&amp;R)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Message &amp;Log</source>
        <translation>消息日志(&amp;L)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Statistics</source>
        <translation>统计(&amp;S)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>&amp;Contents</source>
        <translation>内容(&amp;C)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;About</source>
        <translation>关于(&amp;A)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Re&amp;verse Sort Order</source>
        <translation>倒序(&amp;V)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Name</source>
        <translation>名称(&amp;N)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Files</source>
        <translation>文件(&amp;F)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Tracker</source>
        <translation>Tracker(&amp;T)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Total Ratio</source>
        <translation>总分享率</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Session Ratio</source>
        <translation>会话分享率</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Total Transfer</source>
        <translation>总传输量</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Session Transfer</source>
        <translation>会话传输量</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&amp;Main Window</source>
        <translation>主窗口(&amp;M)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Tray &amp;Icon</source>
        <translation>托盘图标(&amp;I)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>&amp;Change Session…</source>
        <translation>更改会话(&amp;C)…</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Set &amp;Location…</source>
        <translation>设置位置(&amp;L)…</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Open &amp;URL…</source>
        <translation>打开网址(&amp;U)…</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>Choose Session</source>
        <extracomment>Start a local session or connect to a running session</extracomment>
        <translation>选择会话</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>&amp;Copy Magnet Link to Clipboard</source>
        <translation>复制磁力链接到剪贴板(&amp;C)</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&amp;Donate</source>
        <translation>捐赠(&amp;D)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Start &amp;Now</source>
        <translation>现在开始(&amp;N)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Bypass the queue and start now</source>
        <translation>绕开队列并立刻开始</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Move to &amp;Top</source>
        <translation>置顶(&amp;T)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Move &amp;Up</source>
        <translation>上移(&amp;U)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Move &amp;Down</source>
        <translation>下移(&amp;D)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Move to &amp;Bottom</source>
        <translation>置底(&amp;B)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Sort by &amp;Queue</source>
        <translation>按队列排序(&amp;Q)</translation>
    </message>
    <message>
        <location filename="../MainWindow.cc" line="+385"/>
        <source>Limit Download Speed</source>
        <translation>下载限速</translation>
    </message>
    <message>
        <location line="-55"/>
        <source>Unlimited</source>
        <translation>无限制</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+812"/>
        <location line="+9"/>
        <source>Limited at {speed}</source>
        <translation>限制于 {speed}</translation>
    </message>
    <message>
        <location line="-766"/>
        <source>Limit Upload Speed</source>
        <translation>上传限速</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Stop Seeding at Ratio</source>
        <translation>停止做种分享率</translation>
    </message>
    <message>
        <location line="-40"/>
        <source>Seed Forever</source>
        <translation>一直做种</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+799"/>
        <source>Stop at Ratio ({ratio})</source>
        <translation>停止分享率 ({ratio})</translation>
    </message>
    <message>
        <location line="-448"/>
        <source> - {host}:{port}</source>
        <extracomment>Second (optional) part of main window title &quot;Transmission - host:port&quot; (added when connected to remote session) notice that leading space (before the dash) is included here</extracomment>
        <translation> - {host}:{port}</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Idle</source>
        <translation>闲置</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+19"/>
        <source>Ratio: {ratio}</source>
        <translation>分享率: {ratio}</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+7"/>
        <source>Down: {downloaded_size}, Up: {uploaded_size}</source>
        <translation>下载: {downloaded_size}, 上传: {uploaded_size}</translation>
    </message>
    <message>
        <location line="+480"/>
        <source>Torrent Files (*.torrent);;All Files (*.*)</source>
        <translation>种子文件 (*.torrent);;全部文件 (*.*)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Show &amp;options dialog</source>
        <translation>显示选项对话框(&amp;O)</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Open Torrent</source>
        <translation>打开种子</translation>
    </message>
    <message>
        <location line="-1066"/>
        <source>Speed Limits</source>
        <translation>限速</translation>
    </message>
    <message>
        <location line="+546"/>
        <source>Network Error</source>
        <translation>网络错误</translation>
    </message>
    <message>
        <location line="+489"/>
        <source>Click to disable Temporary Speed Limits
 (%1 down, %2 up)</source>
        <translation>关闭临时限速
 (%1 下载, %2 上传)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Click to enable Temporary Speed Limits
 (%1 down, %2 up)</source>
        <translation>开启临时限速
 (%1 下载, %2 上传)</translation>
    </message>
    <message>
        <location line="+160"/>
        <source>Remove torrent?</source>
        <translation>移除种子？</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Delete this torrent&apos;s downloaded files?</source>
        <translation>删除该种子已下载文件？</translation>
    </message>
    <message numerus="yes">
        <location line="-4"/>
        <source>Remove {count:L} torrent(s)?</source>
        <translation>
            <numerusform>移除 {count:L} 个种子？</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="-584"/>
        <source>Showing {visible_count:L} of {count:L} torrent(s)</source>
        <translation>
            <numerusform>显示 {visible_count:L} / {count:L} 个种子</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+589"/>
        <source>Delete these {count:L} torrent(s)&apos; downloaded files?</source>
        <translation>
            <numerusform>删除这 {count:L} 个种子已下载文件？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Once removed, continuing the transfer will require the torrent file or magnet link.</source>
        <translation>移除后，继续传输需要种子文件或磁力链接。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Once removed, continuing the transfers will require the torrent files or magnet links.</source>
        <translation>移除后，继续传输需要种子文件或磁力链接。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>This torrent has not finished downloading.</source>
        <translation>该种子未完成下载。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>These torrents have not finished downloading.</source>
        <translation>这些种子未完成下载。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>This torrent is connected to peers.</source>
        <translation>该种子已连接节点。</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>These torrents are connected to peers.</source>
        <translation>这些种子已连接节点。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>One of these torrents is connected to peers.</source>
        <translation>一个种子已连接节点。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Some of these torrents are connected to peers.</source>
        <translation>一些种子已连接节点。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>One of these torrents has not finished downloading.</source>
        <translation>有一个种子未完成下载。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Some of these torrents have not finished downloading.</source>
        <translation>一些种子未完成下载。</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>{host} has not responded yet</source>
        <translation>{host} 尚未响应</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>{host} is responding</source>
        <translation>{host} 正在响应</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>{host} last responded {time_span} ago</source>
        <translation>{host} 最后响应在 {time_span} 之前</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>{host} is not responding</source>
        <translation>{host} 未响应</translation>
    </message>
    <message>
        <source>Show options dialog</source>
        <translation>显示选项对话框</translation>
    </message>
</context>
<context>
    <name>MakeDialog</name>
    <message>
        <location filename="../MakeDialog.ui" line="+17"/>
        <source>New Torrent</source>
        <translation>新种子</translation>
    </message>
    <message>
        <location filename="../MakeDialog.cc" line="+305"/>
        <source>&lt;i&gt;No source selected&lt;/i&gt;</source>
        <translation>&lt;i&gt;未选择来源&lt;/i&gt;</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>{file_count:L} File(s)</source>
        <translation>
            <numerusform>{file_count:L} 个文件</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>{piece_count:L} Piece(s)</source>
        <translation>
            <numerusform>{piece_count:L} 个区块</numerusform>
        </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>{total_size} in {files}; {pieces} @ {piece_size}</source>
        <translation>{total_size} / {files}; {pieces} @ {piece_size}</translation>
    </message>
    <message>
        <location filename="../MakeDialog.ui" line="+6"/>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Sa&amp;ve to:</source>
        <translation>保存到(&amp;V)：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Source f&amp;older:</source>
        <translation>来源文件夹(&amp;O)：</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Source &amp;file:</source>
        <translation>来源文件(&amp;F)：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Piece s&amp;ize:</source>
        <translation>区块大小(&amp;I):</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Properties</source>
        <translation>属性</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>&amp;Trackers:</source>
        <translation>Tracker(&amp;T)：</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>To add a backup URL, add it on the line after the primary URL.
To add another primary URL, add it after a blank line.</source>
        <translation>要添加一个备用网址，请将其添加到主网址的下一行。
要添加另一个主网址，请将其添加到一个空行的下一行。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Co&amp;mment:</source>
        <translation>说明(&amp;M)：</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>&amp;Source:</source>
        <translation>来源(&amp;S):</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>&amp;Private torrent</source>
        <translation>私有 Torrent(&amp;P)</translation>
    </message>
</context>
<context>
    <name>MakeProgressDialog</name>
    <message>
        <location filename="../MakeProgressDialog.ui" line="+14"/>
        <source>New Torrent</source>
        <translation>新种子</translation>
    </message>
    <message>
        <location filename="../MakeDialog.cc" line="-182"/>
        <source>Creating &quot;%1&quot;</source>
        <translation>正在创建 &quot;%1&quot;</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Created &quot;%1&quot;!</source>
        <translation>已创建 &quot;%1&quot;!</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Couldn&apos;t create &quot;%1&quot;: %2 (%3)</source>
        <translation>无法创建 &quot;%1&quot;: %2 (%3)</translation>
    </message>
</context>
<context>
    <name>OptionsDialog</name>
    <message>
        <location filename="../OptionsDialog.cc" line="+53"/>
        <source>Open Torrent</source>
        <translation>打开种子</translation>
    </message>
    <message>
        <location line="-10"/>
        <source>Open Torrent from File</source>
        <translation>从文件打开种子</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Open Torrent from URL or Magnet Link</source>
        <translation>从网址或磁力链接打开种子</translation>
    </message>
    <message>
        <location filename="../OptionsDialog.ui" line="+17"/>
        <source>&amp;Source:</source>
        <translation>来源(&amp;S)：</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>&amp;Destination folder:</source>
        <translation>目标文件夹(&amp;D)：</translation>
    </message>
    <message>
        <location filename="../OptionsDialog.cc" line="+49"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location filename="../OptionsDialog.ui" line="+35"/>
        <source>&amp;Priority:</source>
        <translation>优先级(&amp;P)：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>S&amp;tart when added</source>
        <translation>添加后开始(&amp;T)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Mo&amp;ve .torrent file to the trash</source>
        <translation>将 .torrent 文件移至回收站(&amp;V)</translation>
    </message>
    <message>
        <location filename="../OptionsDialog.cc" line="-40"/>
        <source>Torrent Files (*.torrent);;All Files (*.*)</source>
        <translation>Torrent 文件（*.torrent）；全部文件（*.*）</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Select Destination</source>
        <translation>选择目标</translation>
    </message>
</context>
<context>
    <name>PathButton</name>
    <message>
        <location filename="../PathButton.cc" line="+23"/>
        <location line="+67"/>
        <source>(None)</source>
        <translation>(无)</translation>
    </message>
    <message>
        <location line="+96"/>
        <source>Select Folder</source>
        <translation>选择文件夹</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Select File</source>
        <translation>选择文件</translation>
    </message>
</context>
<context>
    <name>PrefsDialog</name>
    <message>
        <location filename="../PrefsDialog.ui" line="+971"/>
        <source>Use &amp;authentication</source>
        <translation>使用身份验证(&amp;A)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>&amp;Username:</source>
        <translation>用户名(&amp;U)：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Pass&amp;word:</source>
        <translation>密码(&amp;W)：</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>&amp;Open web client</source>
        <translation>打开 Web 客户端(&amp;O)</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>Addresses:</source>
        <translation>地址：</translation>
    </message>
    <message>
        <location line="-985"/>
        <source>Speed Limits</source>
        <translation>速度限制</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>&lt;small&gt;Override normal speed limits manually or at scheduled times&lt;/small&gt;</source>
        <translation>&lt;small&gt;手动或定时覆盖普通速度限制&lt;/small&gt;</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>&amp;Scheduled times:</source>
        <translation>定时(&amp;S)：</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>&amp;On days:</source>
        <translation>日子(&amp;O)：</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.cc" line="+380"/>
        <source>Every Day</source>
        <translation>每天</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Weekdays</source>
        <translation>工作日</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Weekends</source>
        <translation>周末</translation>
    </message>
    <message>
        <location line="-235"/>
        <source>Sunday</source>
        <translation>星期天</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Monday</source>
        <translation>星期一</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Tuesday</source>
        <translation>星期二</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Wednesday</source>
        <translation>星期三</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Thursday</source>
        <translation>星期四</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Friday</source>
        <translation>星期五</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Saturday</source>
        <translation>星期六</translation>
    </message>
    <message numerus="yes">
        <location line="+464"/>
        <source>{minutes:L} minute(s)</source>
        <extracomment>Spin box format, &quot;Stop seeding if idle for: [ 5 minutes ]&quot;</extracomment>
        <translation>
            <numerusform>{minutes:L} 分钟</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+26"/>
        <source>{minutes_ago:L} minute(s) ago</source>
        <extracomment>Spin box format, &quot;Download is inactive if data sharing stopped: [ 5 minutes ago ]&quot;</extracomment>
        <translation>
            <numerusform>{minutes_ago:L} 分钟前</numerusform>
        </translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Select &quot;Torrent Done Downloading&quot; Script</source>
        <translation>选择 &quot;Torrent 下载完成&quot; 脚本</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.ui" line="+447"/>
        <source>Incoming Peers</source>
        <translation>入站的节点</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>&amp;Port for incoming connections:</source>
        <translation>入站连接的端口(&amp;P)：</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>Use UPnP or NAT-PMP port &amp;forwarding from my router</source>
        <translation>使用路由器的 UPnP 或 NAT-PMP 端口转发(&amp;F)</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.cc" line="-35"/>
        <source>Select &quot;Torrent Done Seeding&quot; Script</source>
        <translation>选择 &quot;Torrent 做种完成&quot; 脚本</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.ui" line="-520"/>
        <source>Automatically add .torrent files &amp;from:</source>
        <translation>自动添加 .torrent 文件，来源为(&amp;F)：</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Show the Torrent Options &amp;dialog</source>
        <translation>显示 Torrent 的选项对话框(&amp;D)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&amp;Start added torrents</source>
        <translation>添加 Torrent 后开始(&amp;S)</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Mo&amp;ve the .torrent file to the trash</source>
        <translation>将 .torrent 文件移至回收站(&amp;V)</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Download Queue</source>
        <translation>下载队列</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Ma&amp;ximum active downloads:</source>
        <translation>最大活动下载数(&amp;X)：</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>Incomplete</source>
        <translation>未完成</translation>
    </message>
    <message>
        <location line="+74"/>
        <source>Seeding</source>
        <translation>做种</translation>
    </message>
    <message>
        <location line="+361"/>
        <source>Trackers to use on all public torrents.

To add a backup URL, add it on the next line after a primary URL.
To add a new primary URL, add it after a blank line.</source>
        <translation>将在所有公开 Torrent 任务上使用的 tracker。

要添加一个备用网址，请在主网址之后一行添加。
要添加另一个主网址，请空一行之后添加。</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>Remote</source>
        <translation>远程</translation>
    </message>
    <message numerus="yes">
        <location filename="../PrefsDialog.cc" line="+180"/>
        <source>&lt;i&gt;Blocklist contains {count:L} rule(s)&lt;/i&gt;</source>
        <translation>
            <numerusform>&lt;i&gt;屏蔽列表包含 {count:L} 条规则&lt;/i&gt;</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../PrefsDialog.ui" line="-236"/>
        <source>Pick a &amp;random port every time Transmission is started</source>
        <translation>Transmission 每次启动时随机选择端口(&amp;R)</translation>
    </message>
    <message>
        <location line="-228"/>
        <source>Limits</source>
        <translation>限制</translation>
    </message>
    <message>
        <location line="+251"/>
        <source>Maximum peers per &amp;torrent:</source>
        <translation>每个 Torrent 的最大节点数量(&amp;T)：</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Maximum peers &amp;overall:</source>
        <translation>总体最大节点数量(&amp;O)：</translation>
    </message>
    <message>
        <location line="-155"/>
        <source>Blocklist</source>
        <translation>屏蔽列表</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Enable &amp;automatic updates</source>
        <translation>启用自动更新(&amp;A)</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.cc" line="-215"/>
        <source>Allow encryption</source>
        <translation>允许加密</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Prefer encryption</source>
        <translation>偏好加密</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Require encryption</source>
        <translation>必须加密</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.ui" line="-64"/>
        <source>Privacy</source>
        <translation>隐私</translation>
    </message>
    <message>
        <location line="-372"/>
        <source>&amp;to</source>
        <translation>到(&amp;T)</translation>
    </message>
    <message>
        <location line="+672"/>
        <location line="+6"/>
        <source>Desktop</source>
        <translation>桌面</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Show {appname} icon in the &amp;notification area</source>
        <translation>在通知区域显示 {appname} 图标(&amp;N)</translation>
    </message>
    <message>
        <location line="-192"/>
        <source>Te&amp;st Port</source>
        <translation>测试端口(&amp;S)</translation>
    </message>
    <message>
        <location line="-86"/>
        <source>Enable &amp;blocklist:</source>
        <translation>启用屏蔽列表(&amp;B)：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>&amp;Update</source>
        <translation>更新(&amp;U)</translation>
    </message>
    <message>
        <location line="-42"/>
        <source>&amp;Encryption mode:</source>
        <translation>加密模式(&amp;E)：</translation>
    </message>
    <message>
        <location line="+367"/>
        <source>Remote Control</source>
        <translation>远程控制</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Allow &amp;remote access</source>
        <translation>允许远程访问(&amp;R)</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>HTTP &amp;port:</source>
        <translation>HTTP 端口(&amp;P)：</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>Only allow these IP a&amp;ddresses:</source>
        <translation>只允许这些 IP 地址(&amp;D)：</translation>
    </message>
    <message>
        <location line="-969"/>
        <source>&amp;Upload:</source>
        <translation>上传(&amp;U)：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>&amp;Download:</source>
        <translation>下载(&amp;D)：</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Alternative Speed Limits</source>
        <translation>备用速度限制</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>U&amp;pload:</source>
        <translation>上传(&amp;P)：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Do&amp;wnload:</source>
        <translation>下载(&amp;W)：</translation>
    </message>
    <message>
        <location line="+142"/>
        <source>Reads user clipboard content for torrents</source>
        <translation>从用户剪贴板读取 Torrent</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Detect new torrents from clipboard</source>
        <translation>从剪贴板检测新的 Torrent</translation>
    </message>
    <message>
        <location line="+136"/>
        <source>Call scrip&amp;t when downloading is completed:</source>
        <translation>下载完成后调用脚本(&amp;T):</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>Call scrip&amp;t when seeding is completed:</source>
        <translation>做种完成后调用脚本(&amp;T):</translation>
    </message>
    <message>
        <location line="+254"/>
        <source>µTP is a tool for reducing network congestion.</source>
        <translation>µTP 是一种减少网络拥堵的工具。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable µ&amp;TP for peer connections</source>
        <translation>为节点连接启用 µTP(&amp;T)</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Default Public Trackers</source>
        <translation>默认公共 Tracker</translation>
    </message>
    <message>
        <location line="+61"/>
        <source>Start &amp;minimized in notification area</source>
        <translation>启动时最小化到通知区域(&amp;M)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Notification</source>
        <translation>通知</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Show a notification when torrents are a&amp;dded</source>
        <translation>当 Torrent 被添加时显示一个通知(&amp;D)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show a notification when torrents &amp;finish</source>
        <translation>当 Torrent 完成时显示一个通知(&amp;F)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Play a &amp;sound when torrents finish</source>
        <translation>当 Torrent 完成时播放提示音(&amp;S)</translation>
    </message>
    <message>
        <location line="-195"/>
        <source>Peer Limits</source>
        <translation>节点限制</translation>
    </message>
    <message>
        <location line="+74"/>
        <source>Use PE&amp;X to find more peers</source>
        <translation>使用 PEX 寻找更多节点(&amp;X)</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>PEX is a tool for exchanging peer lists with the peers you&apos;re connected to.</source>
        <translation>PEX 是一个用来与您所连接的节点交换节点列表的工具。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Use &amp;DHT to find more peers</source>
        <translation>使用 DHT 寻找更多节点(&amp;D)</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>DHT is a tool for finding peers without a tracker.</source>
        <translation>DHT 是一个没有 Tracker 也能寻找节点的工具。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Use &amp;Local Peer Discovery to find more peers</source>
        <translation>使用本地节点发现寻找更多节点(&amp;L)</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>LPD is a tool for finding peers on your local network.</source>
        <translation>LPD 是一个用来发现您本地网络节点的工具。</translation>
    </message>
    <message>
        <location line="-239"/>
        <source>Encryption</source>
        <translation>加密</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.cc" line="+67"/>
        <source>Select Incomplete Directory</source>
        <translation>选择未完成目录</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>Select Watch Directory</source>
        <translation>选择监视目录</translation>
    </message>
    <message>
        <location line="-210"/>
        <source>unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>checking…</source>
        <translation>正在检查…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>open</source>
        <translation>打开</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>closed</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>error</source>
        <translation>出错</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Status: &lt;b&gt;{status}&lt;/b&gt;</source>
        <translation>状态: &lt;b&gt;{status}&lt;/b&gt;</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status: &lt;b&gt;{status_ipv4}&lt;/b&gt; (IPv4), &lt;b&gt;{status_ipv6}&lt;/b&gt; (IPv6)</source>
        <translation>状态: &lt;b&gt;{status_ipv4}&lt;/b&gt; (IPv4), &lt;b&gt;{status_ipv6}&lt;/b&gt; (IPv6)</translation>
    </message>
    <message numerus="yes">
        <location line="+96"/>
        <source>&lt;b&gt;Update succeeded!&lt;/b&gt;&lt;p&gt;Blocklist now has {count:L} rule(s).&lt;/p&gt;</source>
        <translation>
            <numerusform>&lt;b&gt;更新成功！&lt;/b&gt;&lt;p&gt;现在屏蔽列表拥有 {count:L} 条规则。&lt;/p&gt;</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>&lt;b&gt;Update Blocklist&lt;/b&gt;&lt;p&gt;Getting new blocklist…&lt;/p&gt;</source>
        <translation>&lt;b&gt;更新屏蔽列表&lt;/b&gt;&lt;p&gt;正在获取新的屏蔽列表…&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Getting new blocklist…</source>
        <translation>正在获取新的屏蔽列表…</translation>
    </message>
    <message>
        <source>Update Blocklist</source>
        <translation>更新屏蔽列表</translation>
    </message>
    <message>
        <location line="+85"/>
        <source>Select Destination</source>
        <translation>选择目标</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.ui" line="-323"/>
        <source>Adding</source>
        <translation>添加</translation>
    </message>
    <message>
        <location line="+117"/>
        <source>Download is i&amp;nactive if data sharing stopped:</source>
        <extracomment>Please keep this phrase as short as possible, it&apos;s currently the longest and influences dialog width</extracomment>
        <translation>停止下载，如果上次数据共享在(&amp;N):</translation>
    </message>
    <message>
        <location line="-123"/>
        <source>Downloading</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+158"/>
        <source>Append &quot;.&amp;part&quot; to incomplete files&apos; names</source>
        <translation>为未完成的文件名附加 &quot;.part&quot; 扩展名(&amp;P)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Keep &amp;incomplete files in:</source>
        <translation>保存未完成的文件到(&amp;I)：</translation>
    </message>
    <message>
        <location line="-100"/>
        <source>Save to &amp;Location:</source>
        <translation>保存到位置(&amp;L)：</translation>
    </message>
    <message>
        <location line="+173"/>
        <source>Stop seeding at &amp;ratio:</source>
        <translation>停止做种当分享率达到(&amp;R)：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Stop seedi&amp;ng if idle for:</source>
        <translation>停止做种当空闲达到(&amp;N)：</translation>
    </message>
    <message>
        <location line="-467"/>
        <source>{appname} Preferences</source>
        <translation>{appname} 偏好设置</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+604"/>
        <source>Network</source>
        <translation>网络</translation>
    </message>
    <message>
        <location filename="../PrefsDialog.cc" line="+114"/>
        <source>Not supported by remote sessions</source>
        <translation>远程会话不支持</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../Application.cc" line="-266"/>
        <source>B/s</source>
        <translation>B/秒</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>kB/s</source>
        <translation>kB/秒</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>MB/s</source>
        <translation>MB/秒</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>GB/s</source>
        <translation>GB/秒</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TB/s</source>
        <translation>TB/秒</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+7"/>
        <source>B</source>
        <translation>B</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>KiB</source>
        <translation>KiB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>MiB</source>
        <translation>MiB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>GiB</source>
        <translation>GiB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TiB</source>
        <translation>TiB</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>kB</source>
        <translation>kB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>MB</source>
        <translation>MB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>GB</source>
        <translation>GB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TB</source>
        <translation>TB</translation>
    </message>
    <message>
        <location line="+247"/>
        <source>Start Now</source>
        <translation>现在开始</translation>
    </message>
</context>
<context>
    <name>RelocateDialog</name>
    <message>
        <location filename="../RelocateDialog.cc" line="+65"/>
        <source>Select Location</source>
        <translation>选择位置</translation>
    </message>
    <message>
        <location filename="../RelocateDialog.ui" line="+14"/>
        <source>Set Torrent Location</source>
        <translation>设置 Torrent 位置</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Set Location</source>
        <translation>设置位置</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>New &amp;location:</source>
        <translation>新的位置(&amp;L)：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&amp;Move from the current folder</source>
        <translation>从当前文件夹移动(&amp;M)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Local data is &amp;already there</source>
        <translation>本地数据已经在那里(&amp;A)</translation>
    </message>
</context>
<context>
    <name>Session</name>
    <message>
        <location filename="../Session.cc" line="+515"/>
        <source>Error Renaming Path</source>
        <translation>重命名路径出错</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&lt;p&gt;&lt;b&gt;Unable to rename &quot;{old_path}&quot; as &quot;{path}&quot;: {error}.&lt;/b&gt;&lt;/p&gt;&lt;p&gt;Please correct the errors and try again.&lt;/p&gt;</source>
        <translation>&lt;p&gt;&lt;b&gt;无法重命名 &quot;{old_path}&quot; 为 &quot;{path}&quot;: {error}.&lt;/b&gt;&lt;/p&gt;&lt;p&gt;请纠正问题后重试。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Please correct the errors and try again.</source>
        <translation>请纠正问题后重试。</translation>
    </message>
    <message>
        <source>Unable to rename &quot;{old_path}&quot; as &quot;{path}&quot;: {error}.</source>
        <translation>无法重命名 &quot;{old_path}&quot; 为 &quot;{path}&quot;: {error}.</translation>
    </message>
    <message>
        <location line="+565"/>
        <source>Error Adding Torrent</source>
        <translation>添加 Torrent 出错</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>{torrent_name} (copy of {hash})</source>
        <translation>{torrent_name} ({hash} 的副本)</translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>Duplicate Torrent(s)</source>
        <translation>
            <numerusform>重复的 Torrent</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <source>Unable to add {count:L} duplicate torrent(s)</source>
        <translation>
            <numerusform>无法添加 {count:L} 个重复的 Torrent</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>SessionDialog</name>
    <message>
        <location filename="../SessionDialog.ui" line="+14"/>
        <source>Change Session</source>
        <translation>更改会话</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Source</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Start &amp;Local Session</source>
        <translation>开始本地会话(&amp;L)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Connect to &amp;Remote Session</source>
        <translation>连接远程会话(&amp;R)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&amp;Host:</source>
        <translation>主机(&amp;H):</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&amp;Port:</source>
        <translation>端口(&amp;P):</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>&amp;Authentication required</source>
        <translation>需要验证身份(&amp;A)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&amp;Username:</source>
        <translation>用户名(&amp;U):</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Pass&amp;word:</source>
        <translation>密码(&amp;W):</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>RPC URL pa&amp;th:</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>Speed</name>
    <message>
        <location filename="../Speed.h" line="+40"/>
        <location line="+6"/>
        <source>{speed} {arrow}</source>
        <translation>{speed} {arrow}</translation>
    </message>
</context>
<context>
    <name>StatsDialog</name>
    <message>
        <location filename="../StatsDialog.ui" line="+14"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Current Session</source>
        <translation>当前会话</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+129"/>
        <source>Uploaded:</source>
        <translation>已上传:</translation>
    </message>
    <message>
        <location line="-103"/>
        <location line="+129"/>
        <source>Downloaded:</source>
        <translation>已下载:</translation>
    </message>
    <message>
        <location line="-103"/>
        <location line="+129"/>
        <source>Ratio:</source>
        <translation>分享率:</translation>
    </message>
    <message>
        <location line="-103"/>
        <location line="+129"/>
        <source>Duration:</source>
        <translation>时长:</translation>
    </message>
    <message>
        <location line="-100"/>
        <source>Total</source>
        <translation>总计</translation>
    </message>
    <message numerus="yes">
        <location filename="../StatsDialog.cc" line="+63"/>
        <source>Started {count:L} time(s)</source>
        <translation>
            <numerusform>已启动 {count:L} 次</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>Torrent</name>
    <message>
        <location filename="../Torrent.cc" line="+307"/>
        <source>Verifying local data</source>
        <translation>正在验证本地数据</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Seeding</source>
        <translation>正在做种</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Queued for verification</source>
        <translation>等待验证</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Queued for download</source>
        <translation>排队下载</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Queued for seeding</source>
        <translation>排队做种</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Tracker gave a warning: {warning}</source>
        <translation>Tracker 给出一个警告: {warning}</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Tracker gave an error: {error}</source>
        <translation>Tracker 给出一个错误: {error}</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Error: {error}</source>
        <translation>错误: {error}</translation>
    </message>
</context>
<context>
    <name>TorrentDelegate</name>
    <message>
        <location filename="../TorrentDelegate.cc" line="+173"/>
        <source>Magnetized transfer - retrieving metadata (%1%)</source>
        <extracomment>First part of torrent progress string, %1 is the percentage of torrent metadata downloaded</extracomment>
        <translation>磁力传输 - 正在检索元数据 (%1%)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>{current_size} of {complete_size} ({percent_done}%)</source>
        <extracomment>First part of torrent progress string, {current_size} is how much we&apos;ve got, {complete_size} is how much we&apos;ll have when done, {percent_done} is a percentage of the two</extracomment>
        <translation>{current_size} / {complete_size} ({percent_done}%)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>%1 of %2 (%3%), uploaded %4 (Ratio: %5 Goal: %6)</source>
        <extracomment>First part of torrent progress string, %1 is how much we&apos;ve got, %2 is the torrent&apos;s total size, %3 is a percentage of the two, %4 is how much we&apos;ve uploaded, %5 is our upload-to-download ratio, %6 is the ratio we want to reach before we stop uploading</extracomment>
        <translation>%1 / %2 (%3%), 已上传 %4 (分享率: %5 目标: %6)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio})</source>
        <extracomment>First part of torrent progress string, {current_size} is how much we&apos;ve got, {complete_size} is the torrent&apos;s total size, {percent_complete} is a percentage of the two, {uploaded_size} is how much we&apos;ve uploaded, {ratio} is our upload-to-download ratio</extracomment>
        <translation>{current_size} / {complete_size} ({percent_complete}%), 已上传 {uploaded_size} (分享率: {ratio})</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>%1, uploaded %2 (Ratio: %3 Goal: %4)</source>
        <extracomment>First part of torrent progress string, %1 is the torrent&apos;s total size, %2 is how much we&apos;ve uploaded, %3 is our upload-to-download ratio, %4 is the ratio we want to reach before we stop uploading</extracomment>
        <translation>%1, 已上传 %2 (分享率: %3 目标: %4)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>{complete_size}, uploaded {uploaded_size} (Ratio: {ratio})</source>
        <extracomment>First part of torrent progress string, {complete_size} is the torrent&apos;s total size, {uploaded_size} is how much we&apos;ve uploaded, {ratio} is our upload-to-download ratio</extracomment>
        <translation>{complete_size}, 已上传 {uploaded_size} (分享率: {ratio})</translation>
    </message>
    <message>
        <location line="+15"/>
        <source> - {time_span} left</source>
        <extracomment>Second (optional) part of torrent progress string, {time_span} is duration, notice that leading space (before the dash) is included here</extracomment>
        <translation> - {time_span} 剩余</translation>
    </message>
    <message>
        <location line="+6"/>
        <source> - Remaining time unknown</source>
        <extracomment>Second (optional) part of torrent progress string, notice that leading space (before the dash) is included here</extracomment>
        <translation> - 剩余时间未知</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Ratio: {ratio}</source>
        <translation>分享率: {ratio}</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>{time_span} left</source>
        <extracomment>Second (optional) part of torrent progress string, {time_span} is duration, notice that leading space (before the dash) is included here</extracomment>
        <translation>剩余 {time_span}</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remaining time unknown</source>
        <extracomment>Second (optional) part of torrent progress string, notice that leading space (before the dash) is included here</extracomment>
        <translation>剩余时间未知</translation>
    </message>
    <message numerus="yes">
        <location line="+40"/>
        <source>Downloading from {active_count:L} peer(s)</source>
        <extracomment>First part of phrase &quot;Downloading from ... peer(s) and ... web seed(s)&quot;</extracomment>
        <translation>
            <numerusform>正在下载自 {active_count:L} 个节点</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+22"/>
        <source>Seeding to {active_count:L} peer(s)</source>
        <translation>
            <numerusform>正在做种给 {active_count:L} 个节点</numerusform>
        </translation>
    </message>
    <message>
        <location line="+18"/>
        <source> - </source>
        <translation> - </translation>
    </message>
    <message numerus="yes">
        <location line="-50"/>
        <source>Downloading metadata from {active_count:L} peer(s) ({percent_done}% done)</source>
        <translation>
            <numerusform>正在从 {active_count:L} 个节点下载元数据 ({percent_done}% 已完成)</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+15"/>
        <source>Downloading from {active_count:L} of {connected_count:L} connected peer(s)</source>
        <extracomment>First part of phrase &quot;Downloading from ... of ... connected peer(s) and ... web seed(s)&quot;</extracomment>
        <translation>
            <numerusform>正在下载自 {active_count:L} / {connected_count:L} 个已连接节点</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+8"/>
        <source> and {webseed_count:L} web seed(s)</source>
        <extracomment>Second (optional) part of phrase &quot;Downloading from ... of ... connected peer(s) and ... web seed(s)&quot;, notice that leading space (before &quot;and&quot;) is included here</extracomment>
        <translation>
            <numerusform> 和 {webseed_count:L} 个 WEB 种源</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+13"/>
        <source>Seeding to {active_count:L} of {connected_count:L} connected peer(s)</source>
        <translation>
            <numerusform>正在做种给 {active_count:L} / {connected_count:L} 个已连接节点</numerusform>
        </translation>
    </message>
    <message>
        <location line="-95"/>
        <source>Verifying local data ({percent_done}% tested)</source>
        <translation>正在验证本地数据 (已测试 {percent_done}%)</translation>
    </message>
</context>
<context>
    <name>TrackerDelegate</name>
    <message numerus="yes">
        <location filename="../TrackerDelegate.cc" line="+220"/>
        <source>Got a list of{markup_begin} {peer_count:L} peer(s){markup_end} {time_span} ago</source>
        <extracomment>{markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration</extracomment>
        <translation>
            <numerusform>{time_span}前得到一份{markup_begin} {peer_count:L}个节点{markup_end} 的列表</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Peer list request {markup_begin}timed out{markup_end} {time_span} ago; will retry</source>
        <extracomment>{markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration</extracomment>
        <translation>在 {time_span}前节点列表请求{markup_begin}超时{markup_end}；将重试</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Got an error {markup_begin}&quot;{error}&quot;{markup_end} {time_span} ago</source>
        <extracomment>{markup_begin} and {markup_end} are replaced with HTML markup, {error} is error message, {time_span} is duration</extracomment>
        <translation>{time_span}前得到一个错误 {markup_begin}&quot;{error}&quot;{markup_end}</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>No updates scheduled</source>
        <translation>无更新计划任务</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Asking for more peers in {time_span}</source>
        <extracomment>{time_span} is duration</extracomment>
        <translation>将在 {time_span}后请求更多节点</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Queued to ask for more peers</source>
        <translation>已准备请求更多节点</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Asking for peer counts now… &lt;small&gt;{time_span}&lt;/small&gt;</source>
        <extracomment>{time_span} is duration</extracomment>
        <translation>正在请求节点数量… &lt;small&gt;{time_span}&lt;/small&gt;</translation>
    </message>
    <message numerus="yes">
        <location line="-37"/>
        <source>Tracker had{markup_begin} {seeder_count:L} seeder(s){markup_end}</source>
        <extracomment>First part of phrase &quot;Tracker had ... seeder(s) and ... leecher(s) ... ago&quot;, {markup_begin} and {markup_end} are replaced with HTML markup</extracomment>
        <translation>
            <numerusform>Tracker 有{markup_begin} {seeder_count:L}个做种用户{markup_end}</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source> and{markup_begin} {leecher_count:L} leecher(s){markup_end} {time_span} ago</source>
        <extracomment>Second part of phrase &quot;Tracker had ... seeder(s) and ... leecher(s) ... ago&quot;, {markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration; notice that leading space (before &quot;and&quot;) is included here</extracomment>
        <translation>
            <numerusform> 和{markup_begin} {leecher_count:L}个下载用户{markup_end} 在 {time_span}前</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Tracker had {markup_begin}no information{markup_end} on peer counts {time_span} ago</source>
        <extracomment>{markup_begin} and {markup_end} are replaced with HTML markup, {time_span} is duration</extracomment>
        <translation>在 {time_span}前 Tracker 对节点数量{markup_begin}没有信息{markup_end}</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Got a scrape error {markup_begin}&quot;{error}&quot;{markup_end} {time_span} ago</source>
        <extracomment>{markup_begin} and {markup_end} are replaced with HTML markup, {error} is error message, {time_span} is duration</extracomment>
        <translation>{time_span}前遇到抓取错误 {markup_begin}&quot;{error}&quot;{markup_end}</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Asking for more peers now… &lt;small&gt;{time_span}&lt;/small&gt;</source>
        <extracomment>{time_span} is duration</extracomment>
        <translation>正在请求更多节点… &lt;small&gt;{time_span}&lt;/small&gt;</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Asking for peer counts in {time_span}</source>
        <extracomment>{time_span} is duration</extracomment>
        <translation>将在 {time_span}后请求节点数量</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Queued to ask for peer counts</source>
        <translation>已准备请求节点数量</translation>
    </message>
</context>
<context>
    <name>TrackersDialog</name>
    <message>
        <location filename="../TrackersDialog.ui" line="+17"/>
        <source>Edit Trackers</source>
        <translation>编辑 Tracker</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Tracker Announce URLs</source>
        <translation>Tracker 宣告网址</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>To add a backup URL, add it on the next line after a primary URL.</source>
        <translation>要添加一个备用网址，请将其添加到主网址的下一行。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>To add a new primary URL, add it after a blank line.</source>
        <translation>要添加一个新的主网址，请将其添加到一个空行的下一行。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Also see Default Public Trackers in Edit &gt; Preferences &gt; Network</source>
        <translation>另请参见 编辑&gt;首选项&gt;网络 中的默认公共 Tracker</translation>
    </message>
</context>
</TS>
