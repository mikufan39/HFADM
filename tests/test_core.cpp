// HFADM 核心逻辑测试：图号规则 + 零件限制 + 图纸导入（不依赖 GUI）
// 覆盖：建部件/零件、图号唯一性、范围校验、完整图号拼接、移动冲突、复制自动换号、
//       机型下禁建零件、图纸自动命名 {图号}{字母}_{零件名}.pdf、字母版本递增
#include "database/databasemanager.h"
#include "model/deletionplan.h"
#include "service/nodeservice.h"
#include "service/projectservice.h"
#include "service/drawingservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cstdio>

static int s_pass = 0;
static int s_fail = 0;

static void check(bool ok, const QString &name)
{
    if (ok) {
        ++s_pass;
        std::fprintf(stderr, "[PASS] %s\n", name.toUtf8().constData());
    } else {
        ++s_fail;
        std::fprintf(stderr, "[FAIL] %s\n", name.toUtf8().constData());
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 使用独立临时目录，避免污染现有项目
    const QString root = QDir::tempPath() + QStringLiteral("/hfadm_test_") + QString::number(QCoreApplication::applicationPid());
    const QString projectPath = root + QStringLiteral("/AHZ700");
    QDir().mkpath(projectPath);

    ProjectService projectService;
    check(projectService.createProject(projectPath, QStringLiteral("AHZ700")),
          QStringLiteral("创建项目 AHZ700"));

    DatabaseManager *db = projectService.databaseManager();
    NodeService service(db);
    service.setProjectPath(projectPath); // 删除图纸文件需解析项目相对路径

    // 机型根节点
    QVector<HFADMNode> roots;
    check(db->queryChildren(0, roots) && roots.size() == 1 && roots.first().type == NodeType::Aircraft,
          QStringLiteral("根节点为机型"));
    const qint64 aircraftId = roots.first().id;
    check(service.computeFullPartNo(aircraftId) == QStringLiteral("AHZ700"),
          QStringLiteral("机型完整图号 = AHZ700"));

    // 1. 新建部件 + 图号
    check(service.createComponent(aircraftId, QStringLiteral("起落架"), QStringLiteral("3000")),
          QStringLiteral("新建部件 起落架(3000)"));
    QVector<HFADMNode> children;
    db->queryChildren(aircraftId, children);
    const HFADMNode lg = children.first();
    check(service.computeFullPartNo(lg.id) == QStringLiteral("AHZ700.3000"),
          QStringLiteral("部件完整图号 = AHZ700.3000"));

    // 2. 部件图号全机型唯一：重复段号拒绝
    check(!service.createComponent(aircraftId, QStringLiteral("另一部件"), QStringLiteral("3000")),
          QStringLiteral("部件图号重复 3000 被拒绝"));
    // 3. 部件段范围校验
    check(!service.createComponent(aircraftId, QStringLiteral("越界"), QStringLiteral("10000")),
          QStringLiteral("部件图号 10000 超出 0-9999 被拒绝"));
    check(!service.createComponent(aircraftId, QStringLiteral("非数字"), QStringLiteral("abc")),
          QStringLiteral("部件图号非数字被拒绝"));

    // 4. 子部件图号不继承父段（关键规则）
    check(service.createComponent(lg.id, QStringLiteral("液压装置"), QStringLiteral("3010")),
          QStringLiteral("新建子部件 液压装置(3010)"));
    HFADMNode sub;
    {
        QVector<HFADMNode> lgc;
        db->queryChildren(lg.id, lgc);
        for (const HFADMNode &n : lgc) {
            if (n.name == QStringLiteral("液压装置")) {
                sub = n;
            }
        }
    }
    check(sub.id != 0 && service.computeFullPartNo(sub.id) == QStringLiteral("AHZ700.3010"),
          QStringLiteral("子部件完整图号 = AHZ700.3010（不继承 3000）"));

    // 5. 新建零件 + 继承父节点图号
    check(service.createPart(lg.id, QStringLiteral("横杠"), QStringLiteral("06"),
                             QStringLiteral("铝合金"), 2),
          QStringLiteral("新建零件 横杠(06)"));
    HFADMNode partNode;
    {
        QVector<HFADMNode> lgc;
        db->queryChildren(lg.id, lgc);
        for (const HFADMNode &n : lgc) {
            if (n.name == QStringLiteral("横杠")) {
                partNode = n;
            }
        }
    }
    check(partNode.id != 0
              && service.computeFullPartNo(partNode.id) == QStringLiteral("AHZ700.3000.06"),
          QStringLiteral("零件完整图号 = AHZ700.3000.06"));

    // 6. 零件段同父唯一
    check(!service.createPart(lg.id, QStringLiteral("横杠2"), QStringLiteral("06")),
          QStringLiteral("同父零件图号重复 06 被拒绝"));
    // 7. 零件段跨父可相同
    check(service.createPart(sub.id, QStringLiteral("横杠"), QStringLiteral("06"),
                             QStringLiteral("钢"), 1),
          QStringLiteral("跨父零件图号 06 允许（不撞完整图号）"));

    // 8. 深挂零件图号仍三段（关键规则）
    check(service.createPart(sub.id, QStringLiteral("弹簧"), QStringLiteral("07")),
          QStringLiteral("深挂零件 弹簧(07)"));
    {
        QVector<HFADMNode> subc;
        db->queryChildren(sub.id, subc);
        for (const HFADMNode &n : subc) {
            if (n.name == QStringLiteral("弹簧")) {
                check(service.computeFullPartNo(n.id) == QStringLiteral("AHZ700.3010.07"),
                      QStringLiteral("深挂零件完整图号 = AHZ700.3010.07（三段）"));
            }
        }
    }

    // 9. 移动零件：目标父下段号冲突则拒绝
    check(service.createPart(sub.id, QStringLiteral("A件"), QStringLiteral("08")),
          QStringLiteral("新建零件 A件(08) 于液压装置下"));
    check(service.createPart(lg.id, QStringLiteral("B件"), QStringLiteral("08")),
          QStringLiteral("新建零件 B件(08) 于起落架下"));
    {
        qint64 aId = 0;
        QVector<HFADMNode> subc;
        db->queryChildren(sub.id, subc);
        for (const HFADMNode &n : subc) {
            if (n.name == QStringLiteral("A件")) {
                aId = n.id;
            }
        }
        check(!service.moveNode(aId, lg.id),
              QStringLiteral("移动 A件(08) 到已存在 08 的起落架下被拒绝"));
    }

    // 10. 零件创建/移动的父节点限制
    check(!service.createPart(aircraftId, QStringLiteral("非法零件"), QStringLiteral("99")),
          QStringLiteral("机型目录下新建零件被拒绝"));
    {
        qint64 springId = 0;
        QVector<HFADMNode> subc;
        db->queryChildren(sub.id, subc);
        for (const HFADMNode &n : subc) {
            if (n.name == QStringLiteral("弹簧")) {
                springId = n.id;
            }
        }
        check(!service.moveNode(springId, aircraftId),
              QStringLiteral("零件移动到机型下被拒绝"));
    }

    // 11. 图纸导入：自动命名 + 字母版本
    DrawingService drawingService(db);
    drawingService.setProjectPath(projectPath);
    drawingService.setNodeService(&service);

    const QString srcPdf = root + QStringLiteral("/src.pdf");
    const QString srcTxt = root + QStringLiteral("/src.txt");
    {
        QFile f(srcPdf);
        f.open(QIODevice::WriteOnly);
        f.write("%PDF-1.4 test");
        f.close();
        QFile t(srcTxt);
        t.open(QIODevice::WriteOnly);
        t.write("not pdf");
        t.close();
    }

    qint64 ballId = 0;
    check(service.createPart(sub.id, QStringLiteral("球头"), QStringLiteral("10"),
                             QStringLiteral("钢"), 1, &ballId),
          QStringLiteral("新建零件 球头(10)"));
    check(ballId != 0, QStringLiteral("createPart 返回新零件 id"));
    check(drawingService.importPdf(ballId, srcPdf),
          QStringLiteral("首次导入图纸成功"));
    {
        QVector<Drawing> drawings;
        drawingService.queryDrawings(ballId, drawings);
        check(drawings.size() == 1
                  && drawings[0].fileName == QStringLiteral("AHZ700.3010.10A_球头.pdf")
                  && drawings[0].version == QStringLiteral("A"),
              QStringLiteral("首次导入命名 = AHZ700.3010.10A_球头.pdf（版本A）"));
        check(QFile::exists(QDir(projectPath).filePath(
                  DatabaseManager::projectFilesDirectoryName()
                  + QStringLiteral("/") + drawings[0].fileName)),
              QStringLiteral("图纸文件已复制到 files/ 目录"));
    }
    check(drawingService.importPdf(ballId, srcPdf),
          QStringLiteral("再次导入（更新图纸）成功"));
    {
        QVector<Drawing> drawings;
        drawingService.queryDrawings(ballId, drawings);
        // 按创建时间倒序：最新版本 B 显示在最前
        check(drawings.size() == 2
                  && drawings[0].fileName == QStringLiteral("AHZ700.3010.10B_球头.pdf")
                  && drawings[0].version == QStringLiteral("B"),
              QStringLiteral("更新图纸命名 = AHZ700.3010.10B_球头.pdf（版本B，最新在前）"));
    }
    check(!drawingService.importPdf(ballId, srcTxt),
          QStringLiteral("非 PDF 文件导入被拒绝"));

    // 12. 复制部件子树：自动重新分配图号段
    check(service.copyNode(sub.id, aircraftId, QStringLiteral("液压装置 - 副本")),
          QStringLiteral("复制 液压装置 到机型下"));
    {
        bool foundCopy = false;
        QVector<HFADMNode> kids;
        db->queryChildren(aircraftId, kids);
        for (const HFADMNode &n : kids) {
            if (n.name == QStringLiteral("液压装置 - 副本")) {
                foundCopy = true;
                check(!n.partNo.isEmpty() && n.partNo != QStringLiteral("3010"),
                      QStringLiteral("副本部件图号已自动换号: %1").arg(n.partNo));
                // 副本子零件在新父下段号无冲突，应保留原段号（08/07/06/10）
                QVector<HFADMNode> copyKids;
                db->queryChildren(n.id, copyKids);
                QStringList copyPartNos;
                for (const HFADMNode &c : copyKids) {
                    copyPartNos << c.name + QStringLiteral(":") + c.partNo;
                }
                check(copyPartNos.size() == 4
                          && copyPartNos[0] == QStringLiteral("A件:08")
                          && copyPartNos[1] == QStringLiteral("弹簧:07")
                          && copyPartNos[2] == QStringLiteral("横杠:06")
                          && copyPartNos[3] == QStringLiteral("球头:10"),
                      QStringLiteral("副本子零件段号保留且无冲突: %1")
                          .arg(copyPartNos.join(QLatin1Char(' '))));
            }
        }
        check(foundCopy, QStringLiteral("副本已创建于机型下"));
    }

    // 11. 编辑图号：改成同父已用段号拒绝、改合法段号通过
    check(!service.updateNodePartNo(partNode.id, QStringLiteral("08")),
          QStringLiteral("零件图号改成同父已用 08 被拒绝"));
    check(service.updateNodePartNo(partNode.id, QStringLiteral("09")),
          QStringLiteral("零件图号改为 09 成功"));
    check(service.computeFullPartNo(partNode.id) == QStringLiteral("AHZ700.3000.09"),
          QStringLiteral("改号后完整图号 = AHZ700.3000.09"));

    // 12. 机型改名后完整图号跟随刷新（动态拼接）
    check(service.renameNode(aircraftId, QStringLiteral("AHZ710")),
          QStringLiteral("机型改名为 AHZ710"));
    check(service.computeFullPartNo(partNode.id) == QStringLiteral("AHZ710.3000.09"),
          QStringLiteral("机型改名后零件完整图号 = AHZ710.3000.09"));

    // 13. 图纸删除：仅删除（KeepVersions，版本字母空缺保留）
    {
        // 现有 A/B 两版，补到 A/B/C 三版（机型已改名 AHZ710，新版本文件名前缀为 AHZ710）
        check(drawingService.importPdf(ballId, srcPdf),
              QStringLiteral("导入第三版图纸成功"));
        QVector<Drawing> drawings;
        drawingService.queryDrawings(ballId, drawings);
        qint64 bId = 0;
        QString bFileName;
        for (const Drawing &d : drawings) {
            if (d.version == QStringLiteral("B")) {
                bId = d.id;
                bFileName = d.fileName;
            }
        }
        check(bId != 0, QStringLiteral("找到版本 B 图纸"));
        check(drawingService.removeDrawing(bId, DrawingService::DrawingRemovalMode::KeepVersions),
              QStringLiteral("仅删除版本 B 成功"));
        drawingService.queryDrawings(ballId, drawings);
        QStringList versions;
        for (const Drawing &d : drawings) {
            versions << d.version;
        }
        versions.sort();
        check(versions == (QStringList{QStringLiteral("A"), QStringLiteral("C")}),
              QStringLiteral("仅删除后版本为 A、C（B 空缺保留）: %1").arg(versions.join(QLatin1Char(' '))));
        const QString filesDir = QDir(projectPath).filePath(
            DatabaseManager::projectFilesDirectoryName());
        check(!bFileName.isEmpty() && !QFile::exists(QDir(filesDir).filePath(bFileName)),
              QStringLiteral("B 版本磁盘文件已删除: %1").arg(bFileName));
        bool aFileOk = false;
        bool cFileOk = false;
        for (const Drawing &d : drawings) {
            const bool exists = QFile::exists(QDir(filesDir).filePath(d.fileName));
            if (d.version == QStringLiteral("A")) {
                aFileOk = exists;
            }
            if (d.version == QStringLiteral("C")) {
                cFileOk = exists;
            }
        }
        check(aFileOk, QStringLiteral("A 版本磁盘文件保留"));
        check(cFileOk, QStringLiteral("C 版本磁盘文件保留"));
    }

    // 14. 图纸删除：删除并更新图号（Renumber，后续版本前移补位）
    {
        // 当前 A/C，再导入 -> A/C/D
        check(drawingService.importPdf(ballId, srcPdf),
              QStringLiteral("重排测试前导入新版本成功"));
        QVector<Drawing> drawings;
        drawingService.queryDrawings(ballId, drawings);
        qint64 cId = 0;
        QString cFileName;
        QString dFileName;
        for (const Drawing &d : drawings) {
            if (d.version == QStringLiteral("C")) {
                cId = d.id;
                cFileName = d.fileName;
            }
            if (d.version == QStringLiteral("D")) {
                dFileName = d.fileName;
            }
        }
        check(cId != 0, QStringLiteral("找到版本 C 图纸"));
        check(drawingService.removeDrawing(cId, DrawingService::DrawingRemovalMode::Renumber),
              QStringLiteral("删除并更新图号（删 C）成功"));
        drawingService.queryDrawings(ballId, drawings);
        QStringList versions;
        for (const Drawing &d : drawings) {
            versions << d.version;
        }
        versions.sort();
        // A/C/D 删 C 后从 A 重新连续编号：A、B（原 D 前移为 B）
        check(versions == (QStringList{QStringLiteral("A"), QStringLiteral("B")}),
              QStringLiteral("重排后版本为 A、B（原 D 前移为 B）: %1")
                  .arg(versions.join(QLatin1Char(' '))));
        const QString filesDir = QDir(projectPath).filePath(
            DatabaseManager::projectFilesDirectoryName());
        check(!cFileName.isEmpty() && !QFile::exists(QDir(filesDir).filePath(cFileName)),
              QStringLiteral("C 版本磁盘文件已删除: %1").arg(cFileName));
        check(!dFileName.isEmpty() && !QFile::exists(QDir(filesDir).filePath(dFileName)),
              QStringLiteral("原 D 版本磁盘文件已重命名移除: %1").arg(dFileName));
        QString bNewName;
        for (const Drawing &d : drawings) {
            if (d.version == QStringLiteral("B")) {
                bNewName = d.fileName;
            }
        }
        check(!bNewName.isEmpty() && QFile::exists(QDir(filesDir).filePath(bNewName)),
              QStringLiteral("前移后的 B 版本磁盘文件存在: %1").arg(bNewName));
        check(!dFileName.isEmpty() && !bNewName.isEmpty()
                  && bNewName == QString(dFileName).replace(
                      QLatin1Char('D'), QLatin1Char('B')),
              QStringLiteral("重排后数据库记录文件名已更新为 B"));
    }

    // 15. 删除当前版本：isCurrent 自动交接给剩余最新版本
    {
        // 当前 A/B，B 为当前版本；删除 B（KeepVersions）后 A 应成为当前版本
        QVector<Drawing> drawings;
        drawingService.queryDrawings(ballId, drawings);
        qint64 currentId = 0;
        for (const Drawing &d : drawings) {
            if (d.isCurrent) {
                currentId = d.id;
            }
        }
        check(currentId != 0, QStringLiteral("找到当前版本图纸"));
        check(drawingService.removeDrawing(currentId, DrawingService::DrawingRemovalMode::KeepVersions),
              QStringLiteral("删除当前版本成功"));
        drawingService.queryDrawings(ballId, drawings);
        check(drawings.size() == 1 && drawings[0].version == QStringLiteral("A")
                  && drawings[0].isCurrent,
              QStringLiteral("删除当前版本后，剩余 A 自动成为当前版本"));
    }

    // 16. 复制指定图号段（forcedPartNo，供复制冲突弹窗使用）+ 占用检测
    {
        qint64 springId = 0;
        {
            QVector<HFADMNode> subc;
            db->queryChildren(sub.id, subc);
            for (const HFADMNode &n : subc) {
                if (n.name == QStringLiteral("弹簧")) {
                    springId = n.id;
                }
            }
        }
        check(springId != 0, QStringLiteral("找到弹簧零件"));
        check(service.copyNode(springId, sub.id, QStringLiteral("弹簧 - 副本"),
                               QStringLiteral("11")),
              QStringLiteral("指定段号 11 复制零件成功"));
        bool copiedWithPartNo = false;
        {
            QVector<HFADMNode> subc;
            db->queryChildren(sub.id, subc);
            for (const HFADMNode &n : subc) {
                if (n.name == QStringLiteral("弹簧 - 副本") && n.partNo == QStringLiteral("11")) {
                    copiedWithPartNo = true;
                }
            }
        }
        check(copiedWithPartNo, QStringLiteral("复制零件使用指定段号 11"));
        check(service.isPartNoOccupied(NodeType::Part, QStringLiteral("11"), sub.id, 0),
              QStringLiteral("段号 11 在液压装置下已被占用"));
        check(!service.isPartNoOccupied(NodeType::Part, QStringLiteral("99"), sub.id, 0),
              QStringLiteral("段号 99 在液压装置下未被占用"));
    }

    // 17. 物理删除零件：节点+图纸记录+磁盘文件（无回收站）
    {
        // 球头剩余 1 张图纸（版本 A，前序测试已删除 B/C）
        DeletionPlan plan;
        check(service.collectDeletionPlan(ballId, plan) && plan.nodes.size() == 1,
              QStringLiteral("收集删除计划：球头子树 1 个节点"));
        check(plan.drawingCount() == 1 && plan.fileCount() == 1,
              QStringLiteral("收集删除计划：球头 1 张图纸、1 个文件"));
        check(service.deleteNode(ballId), QStringLiteral("物理删除球头成功"));

        HFADMNode gone;
        check(!service.getNode(ballId, gone), QStringLiteral("球头节点记录已删除"));
        QVector<Drawing> drawings;
        drawingService.queryDrawings(ballId, drawings);
        check(drawings.isEmpty(), QStringLiteral("球头图纸记录已删除"));
        const QString filesDir = QDir(projectPath).filePath(
            DatabaseManager::projectFilesDirectoryName());
        bool ballFileLeft = false;
        const QStringList pdfs = QDir(filesDir).entryList(
            QStringList() << QStringLiteral("*.pdf"), QDir::Files);
        for (const QString &name : pdfs) {
            if (name.contains(QStringLiteral("球头"))) {
                ballFileLeft = true;
            }
        }
        check(!ballFileLeft, QStringLiteral("球头图纸磁盘文件已删除"));
    }

    // 18. 物理删除部件子树：级联删除全部子级（含零件与图纸）
    {
        // 液压装置子树：自身 + 横杠06 + 弹簧07 + A件08 + 弹簧-副本11 = 5 个节点（球头已在 17 步删除）
        DeletionPlan plan;
        check(service.collectDeletionPlan(sub.id, plan) && plan.nodes.size() == 5,
              QStringLiteral("收集删除计划：液压装置子树 5 个节点"));
        check(service.deleteNode(sub.id), QStringLiteral("物理删除部件子树成功"));

        HFADMNode gone;
        check(!service.getNode(sub.id, gone), QStringLiteral("部件节点记录已删除"));
        QVector<HFADMNode> lgc;
        db->queryChildren(lg.id, lgc);
        bool subGone = true;
        for (const HFADMNode &n : lgc) {
            if (n.id == sub.id) {
                subGone = false;
            }
        }
        check(subGone, QStringLiteral("部件子树已从父目录消失"));
    }

    // 清理临时目录
    QDir(root).removeRecursively();

    std::fprintf(stderr, "======== 结果: %d 通过, %d 失败 ========\n", s_pass, s_fail);
    return s_fail == 0 ? 0 : 1;
}
